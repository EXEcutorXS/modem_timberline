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

bool StringTransfer::sendString(const char* string, uint16_t stringId, uint8_t toType, uint8_t toAddress)
{
    if (!tx.active)
    {
        beginSend(string, stringId, toType, toAddress);
        return true;
    }

    if (pendingCount >= MAX_PENDING)
        return false; /* queue full — drop, caller can retry later */

    PendingSend& p = pending[pendingCount++];
    p.id = stringId;
    uint16_t len = (uint16_t)strlen(string);
    if (len > sizeof(p.data)-1)
        len = (uint16_t)(sizeof(p.data)-1);
    memcpy(p.data, string, len);
    p.data[len] = 0;
    p.toType    = toType;
    p.toAddress = toAddress;
    return true;
}

void StringTransfer::broadcastNext(uint8_t toType, uint8_t toAddress)
{
    if (regCount == 0)
        return;

    if (broadcastIdx >= regCount)
        broadcastIdx = 0;

    RegEntry& e = regs[broadcastIdx];
    broadcastIdx = (uint8_t)((broadcastIdx + 1) % regCount);

    if (e.buffer)
        sendString(e.buffer, e.id, toType, toAddress);
}

void StringTransfer::sendRequestFrame(uint16_t stringId, uint8_t fromType, uint8_t fromAddress)
{
    can.SendMessage(buildId(61, fromType, fromAddress),
        2, 0xFF,
        (uint8_t)stringId, (uint8_t)(stringId>>8),
        0xFF, 0xFF, 0xFF, 0xFF);
}

StringTransfer::RxSlot* StringTransfer::findRxSlot(uint16_t stringId)
{
    for (uint8_t i = 0; i < RX_SLOTS; i++)
        if (rx[i].active && rx[i].id == stringId)
            return &rx[i];
    return 0;
}

StringTransfer::RxSlot* StringTransfer::freeRxSlot(void)
{
    for (uint8_t i = 0; i < RX_SLOTS; i++)
        if (!rx[i].active)
            return &rx[i];
    return 0;
}

void StringTransfer::beginRequest(RxSlot* slot, uint16_t stringId, uint8_t fromType, uint8_t fromAddress)
{
    slot->id           = stringId;
    slot->active       = true;
    slot->packetsTotal = 0;
    slot->receivedMask = 0;
    slot->fromType     = fromType;
    slot->fromAddress  = fromAddress;
    slot->requestTick  = core.getTick();
    slot->retries      = 0;

    sendRequestFrame(stringId, fromType, fromAddress);
}

void StringTransfer::advanceRxQueue(RxSlot* freedSlot)
{
    if (pendingRxCount == 0)
        return;

    beginRequest(freedSlot, pendingRx[0].id, pendingRx[0].fromType, pendingRx[0].fromAddress);
    for (uint8_t i = 1; i < pendingRxCount; i++)
        pendingRx[i-1] = pendingRx[i];
    pendingRxCount--;
}

void StringTransfer::requestString(uint16_t stringId, uint8_t fromType, uint8_t fromAddress)
{
    if (findRxSlot(stringId))
        return; /* already being fetched */

    for (uint8_t i = 0; i < pendingRxCount; i++)
        if (pendingRx[i].id == stringId)
            return; /* already queued */

    RxSlot* slot = freeRxSlot();
    if (slot)
    {
        beginRequest(slot, stringId, fromType, fromAddress);
        return;
    }

    if (pendingRxCount >= MAX_PENDING_RX)
        return; /* queue full — drop, caller can retry later (e.g. re-visit the screen) */

    PendingRequest& p = pendingRx[pendingRxCount++];
    p.id          = stringId;
    p.fromType    = fromType;
    p.fromAddress = fromAddress;
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

    if (D[0] == 1) /* someone is (about to) push us a string — requested or not */
    {
        RxSlot* slot = findRxSlot(id);
        if (!slot)
        {
            /* Not something we're already fetching. Accept it anyway if we
               own a buffer for this id (unsolicited push, e.g. an on-arrival/
               periodic update), so state doesn't go stale until the next
               explicit pull. */
            if (!findEntry(id))
                return;

            slot = freeRxSlot();
            if (!slot)
            {
                /* All RX_SLOTS busy with other transfers right now. An
                   unsolicited push has no retry of its own — dropping it
                   here would lose the update for good until something else
                   happens to re-request this id (e.g. re-opening a screen).
                   Queue an explicit follow-up request instead, so it still
                   arrives once a slot frees up, via the same retry-safe
                   path as any other request. */
                requestString(id, fromType, fromAddress);
                return;
            }
            slot->id      = id;
            slot->active  = true;
            slot->retries = 0;
        }

        slot->length = (uint16_t)D[4] | ((uint16_t)D[5]<<8);
        if (slot->length > MAX_LEN-1)
            slot->length = MAX_LEN-1;

        slot->packetsTotal = (uint16_t)((slot->length + 4) / 5);
        slot->receivedMask = 0;
        slot->fromType     = fromType;
        slot->fromAddress  = fromAddress;
        slot->requestTick  = core.getTick();

        if (slot->packetsTotal == 0)
        {
            /* Empty string — no data packets will ever follow, so there's
               nothing for onPgn62 to complete on. Finish right here. */
            RegEntry* e = findEntry(id);
            if (e && e->buffer && e->size > 0)
                e->buffer[0] = 0;
            receivedCount++;
            slot->active = false;
            advanceRxQueue(slot);
        }
    }
}

void StringTransfer::onPgn62(const uint8_t* D)
{
    uint16_t id = (uint16_t)D[0] | ((uint16_t)D[1]<<8);

    RxSlot* slot = findRxSlot(id);
    if (!slot || slot->packetsTotal == 0)
        return;

    uint8_t packetNum = D[2];
    if (packetNum >= slot->packetsTotal || packetNum >= 32)
        return;

    for (uint8_t i = 0; i < 5; i++)
    {
        uint16_t idx = (uint16_t)(packetNum*5 + i);
        if (idx >= slot->length)
            break;
        slot->data[idx] = (char)D[3+i];
    }
    slot->receivedMask |= (uint32_t)1u << packetNum;

    if (slot->receivedMask == (((uint32_t)1u << slot->packetsTotal) - 1))
    {
        /* Whole string is in — copy it into the real buffer in one shot
           (same truncate-to-e->size behavior as before, just applied as a
           single bounded block instead of per-byte during reception) so
           nothing outside ever observes a half-written value.

           On the modem this runs straight off the CAN RX interrupt (see
           can.cpp's processCanRxMessage()), so the copy itself can't be
           preempted by main-loop code — but a main-loop reader (e.g.
           DataActualizator::ActualizeInternalData()) mid-copy of this same
           buffer *can* be preempted by this very interrupt. __disable_irq()
           here doesn't protect this write (it's already atomic w.r.t. the
           main loop by virtue of running in the ISR) — it protects whoever
           is reading e->buffer from observing a torn mix of pre/post bytes
           if this fires mid-read. The matching guard is on the read side;
           see ActualizeInternalData(). */
        RegEntry* e = findEntry(id);
        if (e && e->buffer && e->size > 0)
        {
            uint16_t n = slot->length;
            if (n > (uint16_t)(e->size - 1))
                n = (uint16_t)(e->size - 1);
            __disable_irq();
            memcpy(e->buffer, slot->data, n);
            e->buffer[n] = 0;
            __enable_irq();
        }
        receivedCount++;
        slot->active = false;
        advanceRxQueue(slot);
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

                if (pendingCount > 0)
                {
                    beginSend(pending[0].data, pending[0].id, pending[0].toType, pending[0].toAddress);
                    for (uint8_t i = 1; i < pendingCount; i++)
                        pending[i-1] = pending[i];
                    pendingCount--;
                }
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

    for (uint8_t i = 0; i < RX_SLOTS; i++)
    {
        RxSlot& slot = rx[i];
        if (!slot.active)
            continue;

        /* No timeout defined yet (still waiting for the "announce") — poll every
           200ms; once packetsTotal is known, wait out the expected transfer time. */
        uint32_t timeout = slot.packetsTotal > 0 ? (uint32_t)(slot.packetsTotal*5*2) : 200;
        if ((core.getTick() - slot.requestTick) > timeout)
        {
            if (slot.retries < 5)
            {
                slot.retries++;
                slot.requestTick  = core.getTick();
                slot.packetsTotal = 0;
                slot.receivedMask = 0;
                sendRequestFrame(slot.id, slot.fromType, slot.fromAddress);
            }
            else
            {
                /* Nobody answered after several tries — give up on this id so a
                   stuck request can't block everything queued behind it forever. */
                slot.active = false;
                advanceRxQueue(&slot);
            }
        }
    }
}
