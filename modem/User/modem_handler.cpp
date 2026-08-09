#include "modem_handler.h"
#include "log.h"
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

/* ── USB MQTT config override ─────────────────────────────────────────
   Same buffer-then-dispatch shape as sms_emulate()/modem_process_emulated_sms()
   above — see modem_handler.h's comment for why this exists (SMS auth needs
   a PIN/admin phone that may not be set/known yet on a fresh unit; USB is
   physically trusted already). */
static char cfgServer[32]   = {0};
static char cfgLogin[16]    = {0};
static char cfgPassword[24] = {0};
static bool cfgServerPending   = false;
static bool cfgLoginPending    = false;
static bool cfgPasswordPending = false;

extern "C" void usb_set_mqtt_server(const char* value) {
    strncpy(cfgServer, value, sizeof(cfgServer) - 1);
    cfgServer[sizeof(cfgServer) - 1] = 0;
    cfgServerPending = true;
}

extern "C" void usb_set_mqtt_login(const char* value) {
    strncpy(cfgLogin, value, sizeof(cfgLogin) - 1);
    cfgLogin[sizeof(cfgLogin) - 1] = 0;
    cfgLoginPending = true;
}

extern "C" void usb_set_mqtt_password(const char* value) {
    strncpy(cfgPassword, value, sizeof(cfgPassword) - 1);
    cfgPassword[sizeof(cfgPassword) - 1] = 0;
    cfgPasswordPending = true;
}

void modem_process_usb_config(void) {
    bool changed = false;
    if (cfgServerPending) {
        cfgServerPending = false;
        strncpy(modem.mqtt.broker, cfgServer, sizeof(modem.mqtt.broker) - 1);
        modem.mqtt.broker[sizeof(modem.mqtt.broker) - 1] = 0;
        log_info("[USB CFG] server="); log_info(modem.mqtt.broker); log_info("\r\n");
        changed = true;
    }
    if (cfgLoginPending) {
        cfgLoginPending = false;
        strncpy(modem.mqtt.username, cfgLogin, sizeof(modem.mqtt.username) - 1);
        modem.mqtt.username[sizeof(modem.mqtt.username) - 1] = 0;
        log_info("[USB CFG] login="); log_info(modem.mqtt.username); log_info("\r\n");
        changed = true;
    }
    if (cfgPasswordPending) {
        cfgPasswordPending = false;
        strncpy(modem.mqtt.password, cfgPassword, sizeof(modem.mqtt.password) - 1);
        modem.mqtt.password[sizeof(modem.mqtt.password) - 1] = 0;
        log_info("[USB CFG] password updated\r\n");
        changed = true;
    }
    /* flash write handled centrally by dataActualizator.handler(), same as
       the "server"/"login"/"password" SMS commands (Timberline.cpp). */
    if (changed) modem.mqttForceReconnect();
}
