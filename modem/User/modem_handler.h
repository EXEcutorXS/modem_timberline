#ifndef __MODEM_HANDLER_H__
#define __MODEM_HANDLER_H__

#include <stdint.h>
#include "Modem.h"

/* Serial number stored in flash */
extern char serialNumberModem[16];

/* USB bridge mode (passthrough to modem UART) — used by hw_config.c */
extern bool             bridgeMode;
#define BRIDGE_TX_MAX   64
extern volatile uint8_t bridgeTxBuf[BRIDGE_TX_MAX];
extern volatile uint8_t bridgeTxLen;

/* Set by core.cpp watchdog handler */
extern bool isReset;

/* SMS emulation via USB (S <phone> <text>).
   sms_emulate() is called from USB ISR — it only buffers the data.
   Call modem_process_emulated_sms() from the main loop to actually dispatch. */
#ifdef __cplusplus
extern "C" {
#endif
void sms_emulate(const char* phone, const char* message);
#ifdef __cplusplus
}
#endif
void modem_process_emulated_sms(void);

#endif /* __MODEM_HANDLER_H__ */
