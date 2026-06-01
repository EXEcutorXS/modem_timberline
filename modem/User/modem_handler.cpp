#include "modem_handler.h"

char             serialNumberModem[16]       = {0};
bool             bridgeMode                  = false;
volatile uint8_t bridgeTxBuf[BRIDGE_TX_MAX]  = {0};
volatile uint8_t bridgeTxLen                 = 0;
bool             isReset                     = false;

extern "C" void sms_emulate(const char* phone, const char* text) {
    if (modem.onSmsReceived) modem.onSmsReceived(phone, text);
}
