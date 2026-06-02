#include "modem_handler.h"
#include <string.h>

char             serialNumberModem[16]       = {0};
bool             bridgeMode                  = false;
volatile uint8_t bridgeTxBuf[BRIDGE_TX_MAX]  = {0};
volatile uint8_t bridgeTxLen                 = 0;
bool             isReset                     = false;

/* ── SMS emulation buffer ─────────────────────────────────────────────
   sms_emulate() is called from USB ISR — must not do heavy work.
   modem_process_emulated_sms() is called from the main loop.         */
static char emuPhone[16]  = {0};
static char emuText[161]  = {0};
static bool emuPending    = false;

extern "C" void sms_emulate(const char* phone, const char* text) {
    strncpy(emuPhone, phone, sizeof(emuPhone) - 1);
    emuPhone[sizeof(emuPhone) - 1] = 0;
    strncpy(emuText,  text,  sizeof(emuText)  - 1);
    emuText[sizeof(emuText) - 1] = 0;
    emuPending = true;
}

void modem_process_emulated_sms(void) {
    if (!emuPending) return;
    emuPending = false;
    if (modem.onSmsReceived) modem.onSmsReceived(emuPhone, emuText);
}
