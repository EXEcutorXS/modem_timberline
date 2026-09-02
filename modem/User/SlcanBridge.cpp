#include "SlcanBridge.h"
#include "hw_config.h"
#include <string.h>

SlcanBridge slcanBridge;

/* -------------------------------------------------------------------------- */

char SlcanBridge::hexNibble(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

int SlcanBridge::parseHexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void SlcanBridge::usbSend(const char* s)
{
    /* Whole-line atomicity: either the full response fits in the USB TX
       ring buffer right now, or none of it goes out. Without this check, a
       CAN RX burst outrunning the ~1ms USB flush (see Handle_USBAsynchXfer()
       in hw_config.c) could still only get partway through a line before
       USB_Tx_Buffer_FreeSpace() hit 0 and USART_To_USB_Send_Data() started
       silently dropping the rest — leaving a truncated, malformed line in
       the buffer that would desync whatever comes after it just as badly
       as the overwrite bug this was written alongside. Dropping the whole
       frame here instead just means CAN Tool times out waiting for it and
       retries, which it already does. */
    if (USB_Tx_Buffer_FreeSpace() < strlen(s)) return;
    while (*s) USART_To_USB_Send_Data(*s++);
}

/* -------------------------------------------------------------------------- */

int SlcanBridge::processLine(const char* line)
{
    if (!line[0]) return 0;

    char cmd = line[0];

    /* Commands valid before open */
    if (cmd == 'S') { usbSend("\r");        return 1; } /* set bitrate — ignore */
    if (cmd == 'V') { usbSend("V0101\r");   return 1; } /* hw/sw version        */
    if (cmd == 'N') { usbSend("NA000\r");   return 1; } /* serial number        */
    if (cmd == 'F') { usbSend("F00\r");     return 1; } /* status flags: OK     */

    if (cmd == 'O') {
        active = true;
        /* A real CAN adapter just lets the hardware auto-retry a frame
           that lost arbitration or hit a bit error — the modem's own
           normal operation deliberately disables that (NART, see
           Can::initialize()'s comment), which is fine for its own sparse
           traffic but means a single collision permanently drops a frame
           during a fast back-to-back burst like flashing. Only flip this
           while acting as a transparent adapter. */
        can.setAutoRetransmit(true);
        usbSend("\r");
        return 1;
    }
    if (cmd == 'C') {
        active = false;
        can.setAutoRetransmit(false); /* restore the modem's own default */
        usbSend("\r");
        return 1;
    }

    /* TX commands — only when open */
    if (cmd == 'T' || cmd == 't') {
        if (!active) { usbSend("\a"); return 1; }

        bool     ext   = (cmd == 'T');
        uint8_t  idLen = ext ? 8 : 3;
        uint8_t  dlcPos = 1 + idLen;

        if ((uint8_t)strlen(line) < dlcPos + 1u) { usbSend("\a"); return 1; }

        /* Parse ID */
        uint32_t id = 0;
        for (uint8_t i = 1; i <= idLen; i++) {
            int n = parseHexNibble(line[i]);
            if (n < 0) { usbSend("\a"); return 1; }
            id = (id << 4) | (uint32_t)n;
        }

        /* Parse DLC */
        uint8_t dlc = (uint8_t)(line[dlcPos] - '0');
        if (dlc > 8) { usbSend("\a"); return 1; }

        /* Parse data */
        uint8_t data[8] = {0,0,0,0,0,0,0,0};
        for (uint8_t i = 0; i < dlc; i++) {
            int hi = parseHexNibble(line[dlcPos + 1 + i*2]);
            int lo = parseHexNibble(line[dlcPos + 2 + i*2]);
            if (hi < 0 || lo < 0) { usbSend("\a"); return 1; }
            data[i] = (uint8_t)((hi << 4) | lo);
        }

        /* Ack only once the frame is actually queued into a hardware TX
           mailbox — CAN_TransmitMessage() silently drops it instead of
           blocking/queuing if all 3 are still busy (see Can::sendRaw()'s
           own comment), which a back-to-back burst like flashing hits far
           more often than occasional single sends. Acking unconditionally
           here told CAN Tool a frame went out when it sometimes hadn't. */
        if (!can.sendRaw(ext, id, dlc, data)) { usbSend("\a"); return 1; }
        usbSend(ext ? "Z\r" : "z\r");
        return 1;
    }

    /* RTR frames */
    if (cmd == 'R' || cmd == 'r') {
        if (!active) { usbSend("\a"); return 1; }
        usbSend("\a"); /* not implemented */
        return 1;
    }

    return 0; /* not an SLCAN command */
}

/* -------------------------------------------------------------------------- */

void SlcanBridge::onCanRx(CanRxMessage* msg)
{
    if (!active) return;

    char    buf[32];
    uint8_t pos = 0;
    uint8_t dlc = msg->DLC > 8 ? 8 : (uint8_t)msg->DLC;

    if (msg->IDE == CAN_Extended_Id) {
        buf[pos++] = 'T';
        uint32_t id = msg->ExtId;
        for (int i = 7; i >= 0; i--) {
            buf[pos + (uint8_t)i] = hexNibble(id & 0xFu);
            id >>= 4;
        }
        pos += 8;
    } else {
        buf[pos++] = 't';
        uint16_t id = msg->StdId & 0x7FFu;
        for (int i = 2; i >= 0; i--) {
            buf[pos + (uint8_t)i] = hexNibble((uint8_t)(id & 0xFu));
            id >>= 4;
        }
        pos += 3;
    }

    buf[pos++] = (char)('0' + dlc);

    for (uint8_t i = 0; i < dlc; i++) {
        buf[pos++] = hexNibble(msg->Data[i] >> 4);
        buf[pos++] = hexNibble(msg->Data[i] & 0xFu);
    }
    buf[pos++] = '\r';
    buf[pos]   = '\0';

    usbSend(buf);
}

int slcan_process_line(const char* line)
{
    return slcanBridge.processLine(line);
}
