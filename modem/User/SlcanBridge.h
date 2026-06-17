#ifndef SLCAN_BRIDGE_H
#define SLCAN_BRIDGE_H

#include <stdint.h>
#include "can.h"

/* USB-CAN bridge implementing a subset of the SLCAN (Serial Line CAN) protocol.
 *
 * Supported commands (terminated by \r):
 *   S<n>          set bitrate 0-8  — accepted, ignored (bus already running)
 *   O             open channel     — enter bridge mode
 *   C             close channel    — leave bridge mode
 *   T<id8><n><d>  transmit extended 29-bit frame (id: 8 hex, n: dlc digit, d: n*2 hex)
 *   t<id3><n><d>  transmit standard 11-bit frame (id: 3 hex)
 *   F             read status flags  → F00\r
 *   V             hw/sw version      → V0101\r
 *   N             serial number      → NA000\r
 *
 * Received CAN frames (when active) are forwarded as:
 *   T<id8><n><d>\r  for extended frames
 *   t<id3><n><d>\r  for standard frames
 *
 * Responses: \r = OK, \a = error.
 */

class SlcanBridge
{
public:
    bool active;

    SlcanBridge() : active(false) {}
    int  processLine(const char* line);   /* returns 1 if handled, 0 if not SLCAN */
    void onCanRx(CanRxMessage* msg);
    void onCanTx(bool ext, uint32_t id, uint8_t dlc, const uint8_t* data);

private:
    void        usbSend(const char* s);
    static char hexNibble(uint8_t v);
    static int  parseHexNibble(char c);
};

extern SlcanBridge slcanBridge;

/* Called from hw_config.c — returns 1 if the line was consumed as SLCAN */
int slcan_process_line(const char* line);

#endif /* SLCAN_BRIDGE_H */
