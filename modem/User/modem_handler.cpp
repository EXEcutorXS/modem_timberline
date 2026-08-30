#include "modem_handler.h"
#include "log.h"
#include "Version.h"
#include "CanRelay.h"
#include "timberline_sms.h"
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

/* ── USB "set" command ───────────────────────────────────────────────
   Same buffer-then-dispatch shape as sms_emulate()/modem_process_emulated_sms()
   above — see modem_handler.h's comment for why this exists and why it must
   not touch modem.* fields directly from usb_process_line()'s ISR context. */
static char setLine[160]  = {0};
static bool setPending    = false;

extern "C" void usb_set_config_line(const char* line) {
    strncpy(setLine, line, sizeof(setLine) - 1);
    setLine[sizeof(setLine) - 1] = 0;
    setPending = true;
}

void modem_process_usb_set(void) {
    if (!setPending) return;
    setPending = false;

    TlSmsParseResult result;
    tl_parse_commands(setLine, (TlTempUnit)modem.config.tempUnit, result);

    for (uint8_t e = 0; e < result.errCount; e++) {
        log_info("[USB SET] error: "); log_info(result.errors[e]); log_info("\r\n");
    }

    bool mqttChanged = false;

    for (uint8_t i = 0; i < result.cmdCount; i++) {
        const TlSmsCmd& cmd = result.cmds[i];
        switch (cmd.type) {

        case TL_CMD_ADMIN:
            if (!cmd.phone[0]) { log_info("[USB SET] admin: phone number required\r\n"); break; }
            strncpy(modem.config.phones[0], cmd.phone, 15); modem.config.phones[0][15] = '\0';
            log_info("[USB SET] admin updated\r\n");
            break;

        case TL_CMD_PHONE:
            if (cmd.phoneNum < 1 || cmd.phoneNum > 4) break;
            if (!cmd.phone[0]) { log_info("[USB SET] phone: number required\r\n"); break; }
            strncpy(modem.config.phones[cmd.phoneNum], cmd.phone, 15);
            modem.config.phones[cmd.phoneNum][15] = '\0';
            log_info("[USB SET] phone updated\r\n");
            break;

        case TL_CMD_SETPIN:
            memcpy(modem.config.pin, cmd.pin, 5);
            log_info("[USB SET] pin updated\r\n");
            break;

        case TL_CMD_UNIT:
            modem.config.tempUnit = cmd.unit;
            log_info("[USB SET] tempUnit updated\r\n");
            break;

        case TL_CMD_FAULTREPORT:
            modem.config.faultReport = cmd.boolVal;
            log_info("[USB SET] faultReport updated\r\n");
            break;

        case TL_CMD_ACK:
            modem.config.cmdAck = cmd.boolVal;
            log_info("[USB SET] cmdAck updated\r\n");
            break;

        case TL_CMD_LANG:
            modem.config.language = cmd.langArg;
            log_info("[USB SET] language updated\r\n");
            break;

        case TL_CMD_SERVER:
            strncpy(modem.mqtt.broker, cmd.strArg, sizeof(modem.mqtt.broker) - 1);
            modem.mqtt.broker[sizeof(modem.mqtt.broker) - 1] = '\0';
            mqttChanged = true;
            log_info("[USB SET] server updated\r\n");
            break;

        case TL_CMD_LOGIN:
            strncpy(modem.mqtt.username, cmd.strArg, sizeof(modem.mqtt.username) - 1);
            modem.mqtt.username[sizeof(modem.mqtt.username) - 1] = '\0';
            mqttChanged = true;
            log_info("[USB SET] login updated\r\n");
            break;

        case TL_CMD_PASSWORD:
            strncpy(modem.mqtt.password, cmd.strArg, sizeof(modem.mqtt.password) - 1);
            modem.mqtt.password[sizeof(modem.mqtt.password) - 1] = '\0';
            mqttChanged = true;
            log_info("[USB SET] password updated\r\n");
            break;

        case TL_CMD_INTERNET:
            modem.config.useInternet = cmd.boolVal;
            log_info("[USB SET] useInternet updated\r\n");
            break;

        case TL_CMD_ROAMING:
            modem.config.allowRoaming = cmd.boolVal;
            log_info("[USB SET] allowRoaming updated\r\n");
            break;

        case TL_CMD_FORCE_2G:
            modem.config.force2gOnly = cmd.boolVal;
            log_info("[USB SET] force2gOnly updated\r\n");
            break;

        case TL_CMD_APN:
            strncpy(modem.internet.apn, cmd.strArg, sizeof(modem.internet.apn) - 1);
            modem.internet.apn[sizeof(modem.internet.apn) - 1] = '\0';
            log_info("[USB SET] apn updated\r\n");
            break;

        case TL_CMD_APN_USER:
            strncpy(modem.internet.apnUsername, cmd.strArg, sizeof(modem.internet.apnUsername) - 1);
            modem.internet.apnUsername[sizeof(modem.internet.apnUsername) - 1] = '\0';
            log_info("[USB SET] apnUsername updated\r\n");
            break;

        case TL_CMD_APN_PASS:
            strncpy(modem.internet.apnPassword, cmd.strArg, sizeof(modem.internet.apnPassword) - 1);
            modem.internet.apnPassword[sizeof(modem.internet.apnPassword) - 1] = '\0';
            log_info("[USB SET] apnPassword updated\r\n");
            break;

        case TL_CMD_CHECKURL:
            strncpy(modem.internet.internetCheckUrl, cmd.strArg, sizeof(modem.internet.internetCheckUrl) - 1);
            modem.internet.internetCheckUrl[sizeof(modem.internet.internetCheckUrl) - 1] = '\0';
            log_info("[USB SET] internetCheckUrl updated\r\n");
            break;

        default:
            /* Recognised SMS keyword (e.g. a heater/zone/reset/factory
               command) but not a settings field this console command
               exposes — reject rather than silently acting on it. */
            log_info("[USB SET] not a settings field\r\n");
            break;
        }
    }

    /* flash write handled centrally by dataActualizator.handler(), same as
       every other config field — see the "server"/"login"/"password" SMS
       commands (Timberline.cpp). */
    if (mqttChanged) modem.mqttForceReconnect();
}

/* ── "status"/"config" USB CDC dump commands ───────────────────────────
   See modem_handler.h's comment. Plain "key=value\r\n" lines so a PC tool
   can parse with a trivial split-on-'=' — deliberately not the AT-command
   wire format (no relation to the cellular module's own AT commands, these
   never leave the modem). Own local appendUint(), same duplicated-per-file
   convention as Modem.cpp/Timberline.cpp/CanRelay.cpp. */
static int appendUint(char* buf, int n, uint32_t v) {
    char tmp[10]; int t = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0 && t < 10) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t > 0) buf[n++] = tmp[--t];
    return n;
}

static void logKV(const char* key, const char* value) {
    log_info(key); log_info("="); log_info(value); log_info("\r\n");
}

static void logKVUint(const char* key, uint32_t value) {
    char buf[11]; int n = appendUint(buf, 0, value); buf[n] = 0;
    logKV(key, buf);
}

static void logKVBool(const char* key, bool value) {
    logKV(key, value ? "1" : "0");
}

/* "a.b.c.d" — same quad convention as bootloader/firmware versions elsewhere */
static void logKVVersion(const char* key, const uint8_t v[4]) {
    char buf[16]; int n = 0;
    n = appendUint(buf, n, v[0]); buf[n++] = '.';
    n = appendUint(buf, n, v[1]); buf[n++] = '.';
    n = appendUint(buf, n, v[2]); buf[n++] = '.';
    n = appendUint(buf, n, v[3]); buf[n] = 0;
    logKV(key, buf);
}

extern "C" void usb_print_status(void) {
    log_info("[STATUS]\r\n");

    const uint8_t fwVersion[4] = { VERSION_1, VERSION_2, VERSION_3, VERSION_4 };
    logKVVersion("fw", fwVersion);
    logKV("serial", serialNumberModem);

    logKVBool("gsm.registered", modem.network.isRegistered);
    logKVBool("gsm.roaming", modem.network.isRoaming);
    logKVUint("gsm.csq", modem.network.csq);
    logKV("gsm.imei", modem.network.imei);
    logKV("gsm.iccid", modem.network.iccid);
    logKV("gsm.ownNumber", modem.network.ownNumber);
    logKV("gsm.operatorCode", modem.network.operatorCode);
    logKV("gsm.operatorName", modem.network.operatorName);
    logKVUint("gsm.lac", modem.network.lac);
    logKVUint("gsm.cellId", modem.network.cellId);
    logKVUint("gsm.act", modem.network.networkAcT);

    logKVBool("net.connected", modem.internet.isInternetConnected);
    logKV("net.ip", modem.internet.ipAddress);

    logKVBool("mqtt.connected", modem.mqtt.connected);

    logKVUint("ota.status", modem.ota.status);
    logKVUint("ota.page", modem.ota.page);
    logKVUint("ota.pageTotal", modem.ota.pageTotal);
    logKVBool("ota.stagedValid", modem.ota.stagedValid);
    logKV("ota.stagedVersion", modem.ota.stagedVersion);
    logKVUint("ota.stagedBytes", modem.ota.stagedBytes);
    logKVUint("ota.deviceType", modem.ota.deviceType);
    logKVUint("ota.flashBase", modem.ota.flashBase);

    logKVBool("selfota.stagedValid", modem.selfOta.stagedValid);
    logKV("selfota.stagedVersion", modem.selfOta.stagedVersion);
    logKVUint("selfota.stagedBytes", modem.selfOta.stagedBytes);

    logKVUint("autoreg.status", modem.autoRegisterStatus);

    logKVUint("canrelay.status", canRelay.status);
    logKVUint("canrelay.phase", canRelay.phase);
    logKVUint("canrelay.targetType", canRelay.targetType);
    logKVUint("canrelay.targetAddress", canRelay.targetAddress);
    logKVUint("canrelay.fragment", canRelay.fragment);
    logKVUint("canrelay.fragmentTotal", canRelay.fragmentTotal);
    logKVUint("canrelay.fails", canRelay.totalFails);
    logKVUint("canrelay.algorithm", canRelay.algorithm);
    if (canRelay.bootloaderSeen) logKVVersion("canrelay.blVersion", canRelay.bootloaderVersion);

    log_info("[END]\r\n");
}

extern "C" void usb_print_config(void) {
    log_info("[CONFIG]\r\n");

    logKV("cfg.phoneAdmin", modem.config.phones[0]);
    logKV("cfg.phone1", modem.config.phones[1]);
    logKV("cfg.phone2", modem.config.phones[2]);
    logKV("cfg.phone3", modem.config.phones[3]);
    logKV("cfg.phone4", modem.config.phones[4]);
    logKV("cfg.pin", modem.config.pin);
    logKVBool("cfg.useInternet", modem.config.useInternet);
    logKVUint("cfg.tempUnit", modem.config.tempUnit);
    logKVBool("cfg.allowRoaming", modem.config.allowRoaming);
    logKVBool("cfg.force2gOnly", modem.config.force2gOnly);
    logKVBool("cfg.faultReport", modem.config.faultReport);
    logKVBool("cfg.cmdAck", modem.config.cmdAck);
    logKVUint("cfg.language", modem.config.language);

    logKV("mqtt.broker", modem.mqtt.broker);
    logKV("mqtt.username", modem.mqtt.username);
    logKV("mqtt.password", modem.mqtt.password);
    logKVUint("mqtt.telemetryIntervalSec", modem.mqtt.telemetryIntervalSec);

    logKV("net.apn", modem.internet.apn);
    logKV("net.apnUsername", modem.internet.apnUsername);
    logKV("net.apnPassword", modem.internet.apnPassword);
    logKV("net.internetCheckUrl", modem.internet.internetCheckUrl);
    logKV("net.connectionLink", modem.internet.connectionLink);

    log_info("[END]\r\n");
}
