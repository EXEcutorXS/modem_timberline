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

/* "set <name> <value>[,<name2> <value2>...]" USB CDC command
   (usb_process_line() in hw_config.c) — same command syntax and validation
   as an SMS control command (see Library/Sms/timberline_sms.h's
   tl_parse_commands()), applied directly to any Modem::Config/mqtt/internet
   field a PC tool wants to change: admin/phone1-4/setpin/unit/faultreport/
   ack/lang/server/login/password/internet/roaming/2g/apn/apnuser/apnpass.
   Bypasses the SMS admin-phone/PIN auth entirely, since physical USB access
   to the device is its own authorization and there's often no way to learn
   or set the PIN/admin phone over SMS in the first place on a fresh unit —
   same rationale the old server/login/password-only version of this had.
   Same buffer-then-dispatch pattern as sms_emulate(): usb_set_config_line()
   just buffers the raw line (safe to call from USB ISR); modem_process_usb_set()
   parses and applies it to modem.config/mqtt/internet from the main loop —
   touching those fields directly from the ISR would race the main loop
   reading the same fields mid-AT-command-string-build. */
#ifdef __cplusplus
extern "C" {
#endif
void usb_set_config_line(const char* line);
#ifdef __cplusplus
}
#endif
void modem_process_usb_set(void);

/* "status"/"config" USB CDC commands (usb_process_line() in hw_config.c) —
   dump the modem's live state / persisted settings as plain "key=value\r\n"
   lines, framed by a "[STATUS]"/"[CONFIG]" header and a "[END]" trailer, for
   a PC-side tool to parse. Read-only, no reentrancy concerns like
   usb_set_config_line() above (nothing to buffer-then-dispatch), so these
   just run straight from usb_process_line() and log_info() their output
   synchronously, same as the existing "S"/"U" command replies do. */
#ifdef __cplusplus
extern "C" {
#endif
void usb_print_status(void);
void usb_print_config(void);
#ifdef __cplusplus
}
#endif

#endif /* __MODEM_HANDLER_H__ */
