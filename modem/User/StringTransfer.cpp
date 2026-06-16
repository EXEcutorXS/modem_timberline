#include "StringTransfer.h"
#include "can.h"
#include "core.h"
#include <string.h>

StringTransfer stringTransfer;

uint32_t StringTransfer::buildId(uint8_t PGN, uint8_t toType, uint8_t toAddress) const
{
    return ((uint32_t)PGN<<20) | ((uint32_t)toType<<13) | ((uint32_t)toAddress<<10)
         | ((uint32_t)can.idType<<3) | can.idAddress;
}

StringTransfer::RegEntry* StringTransfer::findEntry(uint16_t stringId)
{
    for (uint8_t i = 0; i < regCount; i++)
        if (regs[i].id == stringId)
            return &regs[i];
    return 0;
}

void StringTransfer::registerString(uint16_t stringId, char* buffer, uint16_t bufferSize)
{
    RegEntry* e = findEntry(stringId);
    if (!e && regCount < MAX_REGS)
        e = &regs[regCount++];
    if (!e)
        return;

    e->id     = stringId;
    e->buffer = buffer;
    e->size   = bufferSize;
}

void StringTransfer::beginSend(const char* string, uint16_t stringId, uint8_t toType, uint8_t toAddress)
{
    uint16_t len = (uint16_t)strlen(string);
    if (len > sizeof(tx.data)-1)
        len = (uint16_t)(sizeof(tx.data)-1);

    tx.id           = stringId;
    memcpy(tx.data, string, len);
    tx.length       = len;
    tx.packetsTotal = (uint16_t)((len + 4) / 5);
    tx.packetNum    = 0;
    tx.toType       = toType;
    tx.toAddress    = toAddress;
    tx.active       = true;
    tx.lastSendTick = core.getTick();

    can.SendMessage(buildId(61, toType, toAddress),
        1, 0xFF,
        (uint8_t)stringId, (uint8_t)(stringId>>8),
        (uint8_t)len, (uint8_t)(len>>8),
        0, 0xFF);
}

void StringTransfer::sendString(const char* string, uint16_t stringId, uint8_t toType, uint8_t toAddress)
{
    beginSend(string, stringId, toType, toAddress);
}

void StringTransfer::requestString(uint16_t stringId, uint8_t fromType, uint8_t fromAddress)
{
    rx.id           = stringId;
    rx.active       = true;
    rx.packetsTotal = 0;
    rx.receivedMask = 0;
    rx.fromType     = fromType;
    rx.fromAddress  = fromAddress;
    rx.requestTick  = core.getTick();

    can.SendMessage(buildId(61, fromType, fromAddress),
        2, 0xFF,
        (uint8_t)stringId, (uint8_t)(stringId>>8),
        0xFF, 0xFF, 0xFF, 0xFF);
}

void StringTransfer::onPgn61(uint8_t fromType, uint8_t fromAddress, const uint8_t* D)
{
    uint16_t id = (uint16_t)D[2] | ((uint16_t)D[3]<<8);

    if (D[0] == 2) /* someone requests a string we may own */
    {
        RegEntry* e = findEntry(id);
        if (e && e->buffer)
            beginSend(e->buffer, id, fromType, fromAddress);
        return;
    }

    if (D[0] == 1) /* someone is about to push us a string we asked for */
    {
        if (!rx.active || id != rx.id)
            return;

        rx.length = (uint16_t)D[4] | ((uint16_t)D[5]<<8);
        if (rx.length > MAX_LEN-1)
            rx.length = MAX_LEN-1;

        rx.packetsTotal = (uint16_t)((rx.length + 4) / 5);
        rx.receivedMask = 0;
        rx.fromType     = fromType;
        rx.fromAddress  = fromAddress;
        rx.requestTick  = core.getTick();
    }
}

void StringTransfer::onPgn62(const uint8_t* D)
{
    if (!rx.active || rx.packetsTotal == 0)
        return;

    uint16_t id = (uint16_t)D[0] | ((uint16_t)D[1]<<8);
    if (id != rx.id)
        return;

    uint8_t packetNum = D[2];
    if (packetNum >= rx.packetsTotal || packetNum >= 32)
        return;

    RegEntry* e = findEntry(id);

    for (uint8_t i = 0; i < 5; i++)
    {
        uint16_t idx = (uint16_t)(packetNum*5 + i);
        if (idx >= rx.length)
            break;
        if (e && e->buffer && idx < (uint16_t)(e->size-1))
            e->buffer[idx] = (char)D[3+i];
    }
    rx.receivedMask |= (uint32_t)1u << packetNum;

    if (rx.receivedMask == (((uint32_t)1u << rx.packetsTotal) - 1))
    {
        if (e && e->buffer && rx.length < e->size)
            e->buffer[rx.length] = 0;
        rx.active = false;
    }
}

void StringTransfer::onCanMessage(uint32_t canId, const uint8_t* D)
{
    uint16_t pgn = (uint16_t)((canId>>20) & 0x1FF);
    if (pgn != 61 && pgn != 62)
        return;

    uint8_t fromType    = (uint8_t)((canId>>3) & 0x7F);
    uint8_t fromAddress = (uint8_t)(canId & 0x7);

    if (pgn == 61)
        onPgn61(fromType, fromAddress, D);
    else
        onPgn62(D);
}

void StringTransfer::handler(void)
{
    if (tx.active)
    {
        if ((core.getTick() - tx.lastSendTick) >= 5)
        {
            if (tx.packetNum >= tx.packetsTotal)
            {
                tx.active = false;
            }
            else
            {
                uint8_t d[5] = {0xFF,0xFF,0xFF,0xFF,0xFF};
                for (uint8_t i = 0; i < 5; i++)
                {
                    uint16_t idx = (uint16_t)(tx.packetNum*5 + i);
                    if (idx < tx.length)
                        d[i] = (uint8_t)tx.data[idx];
                }

                can.SendMessage(buildId(62, tx.toType, tx.toAddress),
                    (uint8_t)tx.id, (uint8_t)(tx.id>>8), (uint8_t)tx.packetNum,
                    d[0], d[1], d[2], d[3], d[4]);

                tx.lastSendTick = core.getTick();
                tx.packetNum++;
            }
        }
    }

    if (rx.active && rx.packetsTotal > 0)
    {
        uint32_t timeout = (uint32_t)(rx.packetsTotal*5*2);
        if ((core.getTick() - rx.requestTick) > timeout)
            requestString(rx.id, rx.fromType, rx.fromAddress);
    }
}
