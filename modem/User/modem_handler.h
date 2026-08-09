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

/* Direct MQTT server/login/password override via USB CDC (server <host> /
   login <user> / password <pass> in usb_process_line()) — bypasses the SMS
   admin-phone/PIN auth in tl_sms_parse() entirely, since physical USB access
   to the device is its own authorization and there's often no way to learn
   or set the PIN/admin phone over SMS in the first place on a fresh unit.
   Same buffer-then-dispatch pattern as sms_emulate(): the usb_set_mqtt_*()
   setters just buffer (safe to call from USB ISR); modem_process_usb_config()
   applies the buffered values to modem.mqtt.* and reconnects, from the main
   loop. */
#ifdef __cplusplus
extern "C" {
#endif
void usb_set_mqtt_server(const char* value);
void usb_set_mqtt_login(const char* value);
void usb_set_mqtt_password(const char* value);
#ifdef __cplusplus
}
#endif
void modem_process_usb_config(void);

#endif /* __MODEM_HANDLER_H__ */
