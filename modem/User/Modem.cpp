#include "Modem.h"
#include "modem_handler.h"
#include "core.h"
#include "log.h"
#include "operator_names.h"
#include "flash.h"
#include "Version.h"
#include <string.h>
#include <stdlib.h>

Modem   modem;

/* ARM Compiler 5 (C++03) doesn't have static_assert — classic negative-
   array-size compile-time check instead. A mismatch here means Modem.h's
   MODEM_OTA_PAGE_SIZE/COUNT macros drifted out of sync with flash.h's
   FLASH_PAGE_SIZE/FLASH_OTA_PAGE_COUNT. */
typedef char OtaPageSizeCheck[(MODEM_OTA_PAGE_SIZE == FLASH_PAGE_SIZE) ? 1 : -1];
typedef char OtaPageCountCheck[(MODEM_OTA_PAGE_COUNT == FLASH_OTA_PAGE_COUNT) ? 1 : -1];
typedef char SelfOtaPageCountCheck[(MODEM_SELF_OTA_PAGE_COUNT == FLASH_SELF_OTA_PAGE_COUNT) ? 1 : -1];


extern "C" void USART1_IRQHandler(void) {
    if (USART_GetIntStatus(USART1, USART_INT_RXDNE) != RESET)
        modem.usart.receiveIntHandler((uint8_t)USART_ReceiveData(USART1));
    if (USART_GetIntStatus(USART1, USART_INT_TXDE) != RESET)
        modem.txIsr();
}

/* ── Constructor ─────────────────────────────────────────────────────── */
Modem::Modem()
    : onMqttCommand(0),
      onSmsReceived(0),
      smsDebugMode(false),
      answer(0), capture(CAP_NONE),
      state(ST_POWER_ON), step(0),
      ussdPending(false)
{
    /* Nested structs (network/internet/mqtt/ota/config, and the private
       scratch ones: sms/timers/http/rawCapture/mqttScratch/otaScratch/rx)
       aren't aggregate-initialized in the member-init list above — ARM
       Compiler 5's C++ support doesn't reliably take brace-init there, and
       a member-initializer-list entry can only name a *direct* member (e.g.
       `network(...)`, not `network.isRegistered(...)`) — so their fields
       are just zeroed/defaulted here instead. */
    network.isRegistered = false; network.isRoaming = false; network.csq = 0xFF;
    csRegistered = csRoaming = epsRegistered = epsRoaming = false;
    network.lac = 0xFFFF; network.cellId = 0xFFFFFFFF; network.networkAcT = 0xFF;
    internet.isInternetConnected = false;
    mqtt.connected = false;
    mqtt.telemetryIntervalSec = 15;
    config.useInternet = false;
    config.tempUnit = 0;
    config.allowRoaming = false;
    config.faultReport = false;
    config.cmdAck = true;
    ota.status = OTA_IDLE; ota.page = 0; ota.pageTotal = 0;
    ota.stagedValid = false; ota.stagedVersion[0] = 0; ota.stagedBytes = 0;
    ota.deviceType = 0; ota.flashBase = 0;

    sms.pending = false; sms.slot = 0; sms.notifySlot = 0; sms.notifyPending = false;
    timers.csq = 0; timers.creg = 0; timers.net = 0; timers.mqttRetry = 0; timers.smsPoll = 0;
    http.status = 0; http.dataLen = 0;
    rawCapture.dst = 0; rawCapture.cap = 0; rawCapture.got = 0; rawCapture.chunkRemaining = 0;
    mqttScratch.urcResult = 0; mqttScratch.teardownThenNet = false;
    mqttScratch.netTeardownThenReinit = false; mqttScratch.reconnectRequested = false;
    otaScratch.startRequested = false; otaScratch.retries = 0; otaScratch.failed = false; otaScratch.readLen = 0;
    regScratch.startRequested = false; regScratch.retries = 0;
    regScratch.login[0] = 0; regScratch.password[0] = 0;
    autoRegisterStatus = AUTOREG_IDLE;
    rx.cursor = 0; rx.len = 0;

    network.imei[0] = network.iccid[0] = network.ownNumber[0] = network.operatorCode[0] = network.operatorName[0] = 0;
    network.smsPhone[0] = network.smsText[0] = network.cmgrPhone[0] = network.cmgrBody[0] = ussdReq[0] = 0;
    internet.ipAddress[0] = 0;
    /* example.com (IANA/ICANN-run, reserved for documentation/testing) —
       globally reachable including from behind the Great Firewall, unlike
       google.com, and backed by Verisign's infrastructure for uptime.
       See "internet check url" in the SMS command set if this ever needs
       overriding for a specific deployment. */
    strncpy(internet.internetCheckUrl, "http://example.com", sizeof(internet.internetCheckUrl) - 1);
    internet.internetCheckUrl[sizeof(internet.internetCheckUrl) - 1] = 0;
    /* Broker gets a baked-in fallback (see MODEM_DEFAULT_BROKER in Modem.h)
       so a factory-fresh device can auto-register (startAutoRegister())
       without ever having been told a broker via SMS first — mirrored in
       flash.cpp's sanitizeString() call for mqtt.broker, which is what
       actually wins once readSetup() runs right after this constructor;
       this initial value only matters for the brief window before that.
       Login/password stay genuinely empty on purpose — a real account's
       credentials don't belong compiled into firmware that could end up on
       any device; those only ever come from SMS/CAN or auto-registration. */
    strncpy(mqtt.broker, MODEM_DEFAULT_BROKER, sizeof(mqtt.broker) - 1);
    mqtt.broker[sizeof(mqtt.broker) - 1] = 0;
    mqtt.username[0] = 0;
    mqtt.password[0] = 0;
    internet.apn[0] = 0;
    internet.apnUsername[0] = 0;
    internet.apnPassword[0] = 0;
    mqttScratch.rxName[0] = 0; mqttScratch.rxPayload[0] = 0;
    otaScratch.version[0] = 0;
    otaScratch.deviceType = 0;
    for (int i = 0; i < MQTT_PUB_MAX; i++) {
        mqttScratch.pubQueue[i].name[0] = 0;
        mqttScratch.pubQueue[i].value[0] = 0;
        mqttScratch.pubQueue[i].dirty = false;
    }
    /* "online" pre-claims slot 0 (not marked dirty yet — just reserved by
       name) so doMqttPub()'s scan, which always picks the lowest-index dirty
       entry, drains it before anything else queued around the same time.
       Confirmed on real hardware: right after reconnecting, many actualizer
       fields go dirty at once (a full resend on justConnected), and "online"
       otherwise lands wherever mqttPublish() first happened to see it —
       which could be dozens of entries deep — leaving the browser looking at
       the stale retained "0" from the Last Will for a few extra seconds
       after the device is actually back. Reserving slot 0 makes the
       retained-"1" overwrite go out first, every time. */
    strncpy(mqttScratch.pubQueue[0].name, "online", sizeof(mqttScratch.pubQueue[0].name) - 1);
    for (int i = 0; i < SMS_QUEUE_MAX; i++) {
        sms.queue[i].phone[0] = 0;
        sms.queue[i].text[0] = 0;
        sms.queue[i].pending = false;
    }
    for (int i = 0; i < 5; i++) config.phones[i][0] = 0;
    config.pin[0]='1'; config.pin[1]='2'; config.pin[2]='3'; config.pin[3]='4'; config.pin[4]='\0';
		
}

void Modem::txIsr(void) { usart.transmitNextByte(); }

/* ── initialize ──────────────────────────────────────────────────────── */
void Modem::initialize(void) {
		config.useInternet = true;
    usart.initialize(1, 115200);
    PowergoodPin.Initialize(GPIOA, GPIO_PIN_3, GPIO_Mode_IPU);
    DTRPin.Initialize(GPIOB, GPIO_PIN_1, GPIO_Mode_Out_PP);
    DTRPin.Reset();
    PowerkeyPin.Initialize(GPIOB, GPIO_PIN_7, GPIO_Mode_Out_PP);
    PowerkeyPin.Reset();
    refreshStagedInfo();
}

/* See ota.stagedValid/stagedVersion/stagedBytes in Modem.h. flash.h doesn't
   need FLASH_Unlock() for a plain read, so this is cheap enough to call any
   time the flash contents might have changed — right after boot and right
   after doOta() finishes. */
void Modem::refreshStagedInfo(void) {
    uint16_t crcUnused;
    /* flashBase is persisted alongside version/bytes now (see
       Flash_C::writeOtaMeta()'s own comment) — fixed 2026-08-29 after a
       real bricking incident traced to flashBase staying 0 across a
       reboot while the web UI, thanks to the deviceType-restore fix right
       below, showed "Loaded, ready to Flash" as if everything were fine.
       Timberline::doCanRelay() still has its own flashBase==0 refuse-to-
       proceed check as a hard backstop — this restore just means that
       check won't normally fire any more. */
    ota.stagedValid = flash.readOtaMeta(ota.stagedVersion, &ota.stagedBytes, &crcUnused, &ota.flashBase);
    if (!ota.stagedValid) { ota.stagedVersion[0] = 0; ota.stagedBytes = 0; ota.flashBase = 0; }
    else {
        /* ota.deviceType itself isn't persisted directly — but a version
           string's first component IS the device type by convention (see
           the CAN protocol doc — every version quad's first byte is the
           product type), so it can be recovered from stagedVersion alone.
           Confirmed missing on real hardware 2026-08-29: after a modem
           reboot, otaStaged correctly republished the staged version, but
           otaStagedType stayed 0 (never having been re-derived), so the web
           UI's per-device-card matching (app.js, `dev.type === stagedType`)
           never matched any real device and the card silently showed
           nothing staged, looking exactly like the modem had "forgotten"
           what firmware it was holding. */
        uint32_t t = 0;
        for (const char* p = ota.stagedVersion; *p && *p != '.'; p++) {
            if (*p < '0' || *p > '9') { t = 0; break; }
            t = t * 10 + (uint32_t)(*p - '0');
        }
        ota.deviceType = (t <= 255) ? (uint8_t)t : 0;
    }

    uint16_t selfCrcUnused;
    selfOta.stagedValid = flash.readSelfOtaMeta(selfOta.stagedVersion, &selfOta.stagedBytes, &selfCrcUnused);
    if (!selfOta.stagedValid) { selfOta.stagedVersion[0] = 0; selfOta.stagedBytes = 0; }
}

/* ── handler ─────────────────────────────────────────────────────────── */
void Modem::handler(void) {
    if (!PowergoodPin.Get()) return;
    drainRx();

    /* Bridge mode: forward USB→modem, state machine paused.
       AT commands need \r as terminator; terminals often send \n only —
       expand each \n to \r\n so the modem sees a proper line ending.    */
    if (bridgeMode) {
        uint8_t len = bridgeTxLen;
        if (len > 0) {
            static uint8_t local[BRIDGE_TX_MAX * 2];
            uint8_t out = 0;
            for (uint8_t i = 0; i < len && out < sizeof(local) - 1; i++) {
                uint8_t c = bridgeTxBuf[i];
                if (c == '\n') local[out++] = '\r'; /* ensure CR before LF */
                local[out++] = c;
            }
            bridgeTxLen = 0;
            usart.send(local, out);
        }
        return;
    }

    switch (state) {
        case ST_POWER_ON:   doPowerOn();   break;
        case ST_WAIT_READY: doWaitReady(); break;
        case ST_INIT:       doInit();      break;
        case ST_IDLE:       doIdle();      break;
        case ST_READ_SMS:   doReadSms();   break;
        case ST_SEND_SMS:   doSendSms();   break;
        case ST_POLL_CSQ:   doPollCsq();   break;
        case ST_POLL_CREG:  doPollCreg();  break;
        case ST_POLL_SMS_UNREAD: doPollSmsUnread(); break;
        case ST_USSD:       doUssd();      break;
        case ST_INIT_NET:      doInitNet();      break;
        case ST_CHECK_INTERNET: doCheckInternet(); break;
        case ST_NET_TEARDOWN:  doNetTeardown();  break;
        case ST_MQTT_START:    doMqttStart();    break;
        case ST_MQTT_SUB:      doMqttSub();      break;
        case ST_MQTT_PUB:      doMqttPub();      break;
        case ST_MQTT_TEARDOWN: doMqttTeardown(); break;
        case ST_FETCH_PROFILE: doFetchProfile(); break;
        case ST_OTA:           doOta();          break;
        case ST_AUTO_REGISTER: doAutoRegister(); break;
    }
}

/* ── sendSms ─────────────────────────────────────────────────────────── */
void Modem::sendSms(const char* phone, const char* text) {
    if (!phone || phone[0] != '+') return;

    log_info("[SMS] to: "); log_info(phone);
    log_info(" | ");        log_info(text);
    log_info("\r\n");

    if (smsDebugMode) return;

    if (!sms.pending) {
        strncpy(network.smsPhone, phone, sizeof(network.smsPhone) - 1); network.smsPhone[sizeof(network.smsPhone)-1] = 0;
        strncpy(network.smsText,  text,  sizeof(network.smsText)  - 1); network.smsText[sizeof(network.smsText) -1] = 0;
        sms.pending = true;
        return;
    }

    /* Active slot busy — queue it instead of silently dropping (see
       SmsQueueEntry comment in Modem.h). Never calls setState(): sendSms()
       can be invoked reentrantly from inside onSmsReceived (itself called
       from doReadSms()), same constraint as mqttForceReconnect(). */
    for (int i = 0; i < SMS_QUEUE_MAX; i++) {
        if (!sms.queue[i].pending) {
            strncpy(sms.queue[i].phone, phone, sizeof(sms.queue[i].phone) - 1);
            sms.queue[i].phone[sizeof(sms.queue[i].phone) - 1] = 0;
            strncpy(sms.queue[i].text, text, sizeof(sms.queue[i].text) - 1);
            sms.queue[i].text[sizeof(sms.queue[i].text) - 1] = 0;
            sms.queue[i].pending = true;
            return;
        }
    }
    /* Queue also full — drop (same defensive behavior as before, just far
       less likely to actually happen now). */
}

/* ═══════════════════════════════════════════════════════════════════════
   AT engine
   ═══════════════════════════════════════════════════════════════════════*/

/* Lower 6 decimal digits of the millisecond tick, zero-padded — enough to
   read AT round-trip time straight off the log (wraps every 1000s, but a
   wrap mid-command is obvious from context and not worth extra bytes to
   handle). Written to log_at only, not the other log_* streams. */
static void logAtTimestamp(void) {
    uint32_t v = core.getTick() % 1000000;
    char buf[10];
    buf[0] = '[';
    buf[1] = (char)('0' + (v / 100000) % 10);
    buf[2] = (char)('0' + (v / 10000)  % 10);
    buf[3] = (char)('0' + (v / 1000)   % 10);
    buf[4] = (char)('0' + (v / 100)    % 10);
    buf[5] = (char)('0' + (v / 10)     % 10);
    buf[6] = (char)('0' + (v)          % 10);
    buf[7] = ']'; buf[8] = ' '; buf[9] = 0;
    log_at(buf);
}

void Modem::transmit(const char* s) {
    static char buf[512];
    uint16_t n = 0;
    while (*s && n < 511) buf[n++] = *s++;
    if (!n) return;
    buf[n] = 0;                  /* null-terminate for log_at */
    usart.send((uint8_t*)buf, n);
    logAtTimestamp();
    log_at(">> "); log_at(buf);
}

/* Call repeatedly from one state step until it returns true.
   Detects a new command by hash and re-sends automatically.            */
bool Modem::atCmd(const char* cmd, uint32_t ms) {
    static uint32_t h0 = 0, t0 = 0;

    /* djb2 hash */
    uint32_t h = 5381;
    for (const char* p = cmd; *p; p++) h = h * 33 ^ (uint8_t)*p;

    if (h != h0) {
        h0 = h; answer = 0;
        t0 = core.getTick();
        transmit(cmd);
        return false;
    }
    if (answer & (ANS_OK | ANS_ERROR | ANS_PROMPT)) return true;
    if ((core.getTick() - t0) >= ms) { answer |= ANS_TIMEOUT; h0 = 0; return true; }
    return false;
}

/* ── drainRx ─────────────────────────────────────────────────────────── */
void Modem::drainRx(void) {
    static bool atLineStart = true;   /* only the log_at path uses this */

    while (rx.cursor != usart.getBufferPos()) {
        char c = (char)usart.getByte(rx.cursor++);
        if (rx.cursor >= Usart_C::BUFFER_SIZE) rx.cursor = 0;

        /* Raw binary capture (AT+HTTPREAD's payload — see doOta()) bypasses
           everything below: line-splitting on '\r'/'\n' would corrupt binary
           data that legitimately contains those byte values, and logging
           2 KB of binary per page would flood the AT terminal. A step arms
           this by pointing rawCapture.dst at its buffer *before* issuing
           AT+HTTPREAD, but with chunkRemaining still 0 — so each "+HTTPREAD:
           <len>" chunk header itself still goes through normal line parsing
           below (needed so parseLine() can see it and arm chunkRemaining
           from it); only once chunkRemaining is actually set does this gate
           start diverting that chunk's raw bytes. A single AT+HTTPREAD can
           involve several such chunks in a row before the terminator (see
           the RawCapture comment in Modem.h) — got keeps accumulating across
           all of them, only reset when a step re-arms dst for a fresh read. */
        if (rawCapture.dst && rawCapture.chunkRemaining > 0) {
            if (rawCapture.got < rawCapture.cap) rawCapture.dst[rawCapture.got] = (uint8_t)c;
            rawCapture.got++;
            rawCapture.chunkRemaining--;
            continue;
        }

        char dbg[2] = {c, 0};
        if (bridgeMode) {
            log_info(dbg);
        } else {
            if (atLineStart) { logAtTimestamp(); atLineStart = false; }
            log_at(dbg);
            if (c == '\n') atLineStart = true;
        }

        /* SMS prompt arrives as "> " without newline */
        if (c == '>' && rx.len == 0) { answer |= ANS_PROMPT; continue; }

        if (c == '\n') {
            rx.buf[rx.len] = 0;
            parseLine();
            rx.len = 0;
        } else if (c != '\r' && rx.len < LINE_SIZE - 1) {
            rx.buf[rx.len++] = c;
        }
    }
}

/* ── parseLine ───────────────────────────────────────────────────────── */

static bool starts(const char* s, const char* pre) {
    while (*pre) if (*s++ != *pre++) return false;
    return true;
}

/* Append the decimal digits of v to buf starting at n, return new n.
   Takes uint32_t (widened from uint16_t) — doOta()'s HTTPREAD byte offsets
   run up to 63*2048=129024, which doesn't fit 16 bits; existing uint16_t
   callers widen implicitly with no behavior change. tmp[10] holds up to
   4294967295 (10 digits). */
static int appendUint(char* buf, int n, uint32_t v) {
    char tmp[10]; int t = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0 && t < 10) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t > 0) buf[n++] = tmp[--t];
    return n;
}

/* Extract the Nth (0-based) quoted field from s into out. */
static void nthQuoted(const char* s, int n, char* out, int outLen) {
    int q = 0, i = 0;
    out[0] = 0;
    for (; *s; s++) {
        if (*s == '"') { q++; continue; }
        if (q == n*2+1 && i < outLen-1) out[i++] = *s;
        else if (q > n*2+1) break;
    }
    out[i] = 0;
}

/* Some operators deliver non-Latin SMS/USSD text (Cyrillic, German umlauts,
   ...) as UCS2 even though CSCS stays "IRA" — it then arrives as a run of
   hex digits, 4 per UTF-16BE code unit, instead of being auto-decoded. Detect
   that shape (all hex digits, length a multiple of 4) and convert to UTF-8;
   otherwise just copy the text through unchanged (plain GSM7/ASCII stays
   ASCII). Returns true if the input was decoded as UCS2 hex. */
static bool decodeUcs2Hex(const char* raw, char* out, int outCap) {
    int rlen = (int)strlen(raw);
    bool isUcs2 = (rlen >= 4) && (rlen % 4 == 0);
    for (int i = 0; i < rlen && isUcs2; i++) {
        char c = raw[i];
        if (!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f')))
            isUcs2 = false;
    }
    if (!isUcs2) {
        strncpy(out, raw, outCap - 1); out[outCap-1] = 0;
        return false;
    }
    int di = 0;
    for (int i = 0; i < rlen - 3 && di < outCap - 4; i += 4) {
        #define HV(c) ((uint8_t)((c)>='a'?(c)-'a'+10:(c)>='A'?(c)-'A'+10:(c)-'0'))
        uint16_t cp = (uint16_t)( ((uint16_t)HV(raw[i  ])<<12)
                                | ((uint16_t)HV(raw[i+1])<< 8)
                                | ((uint16_t)HV(raw[i+2])<< 4)
                                |  (uint16_t)HV(raw[i+3]));
        #undef HV
        if (cp < 0x80) {
            out[di++] = (char)cp;
        } else if (cp < 0x800) {
            out[di++] = (char)(0xC0 | (cp >> 6));
            out[di++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[di++] = (char)(0xE0 | ( cp >> 12));
            out[di++] = (char)(0x80 | ((cp >>  6) & 0x3F));
            out[di++] = (char)(0x80 | ( cp        & 0x3F));
        }
    }
    out[di] = 0;
    return true;
}

void Modem::parseLine(void) {
    const char* s = rx.buf;

    /* Multi-line capture takes priority */
    if (capture == CAP_IMEI) {
        /* IMEI is plain digits (15 of them). An unsolicited notification
           (e.g. "+CGEV: EPS PDN ACT ...") can land in this same window —
           don't mistake it for the IMEI; keep waiting for the real line. */
        bool looksLikeImei = (rx.len >= 14);
        for (uint16_t i = 0; looksLikeImei && i < rx.len; i++)
            if (s[i] < '0' || s[i] > '9') looksLikeImei = false;

        if (looksLikeImei) {
            strncpy(network.imei, s, sizeof(network.imei)-1); network.imei[sizeof(network.imei)-1] = 0;
            capture = CAP_NONE;
        }
        return;
    }
    if (capture == CAP_CMGR_BODY) {
        capture = CAP_NONE;
        decodeUcs2Hex(s, network.cmgrBody, sizeof(network.cmgrBody));
        return;
    }
    if (capture == CAP_MQTT_TOPIC) {
        capture = CAP_NONE;
        /* Keep everything after the 3rd '/' (i.e. past "<user>/cmd/desired/") —
           most names are a single segment ("btnHtr"), but grouped zone names
           carry one more level ("zn1/state"), so we can't just take the last
           segment like other capture modes do. */
        const char* rest = s;
        int slashes = 0;
        for (const char* p = s; *p; p++) {
            if (*p == '/') { slashes++; if (slashes == 3) { rest = p + 1; break; } }
        }
        strncpy(mqttScratch.rxName, rest, sizeof(mqttScratch.rxName)-1); mqttScratch.rxName[sizeof(mqttScratch.rxName)-1] = 0;
        /* Defensively trim trailing whitespace — some MQTT clients/UIs are
           prone to a stray trailing space in a hand-typed topic. */
        for (int i = (int)strlen(mqttScratch.rxName) - 1; i >= 0 && mqttScratch.rxName[i] == ' '; i--) mqttScratch.rxName[i] = 0;
        return;
    }
    if (capture == CAP_MQTT_PAYLOAD) {
        capture = CAP_NONE;
        strncpy(mqttScratch.rxPayload, s, sizeof(mqttScratch.rxPayload)-1); mqttScratch.rxPayload[sizeof(mqttScratch.rxPayload)-1] = 0;
        for (int i = (int)strlen(mqttScratch.rxPayload) - 1; i >= 0 && mqttScratch.rxPayload[i] == ' '; i--) mqttScratch.rxPayload[i] = 0;
        if (onMqttCommand) onMqttCommand(mqttScratch.rxName, mqttScratch.rxPayload);
        return;
    }

    if (rx.len == 0) return;

    if (strcmp(s, "OK") == 0) {
        answer |= ANS_OK;
    }
    else if (starts(s,"ERROR") || starts(s,"+CME ERROR") || starts(s,"+CMS ERROR")) {
        answer |= ANS_ERROR;
    }
    else if (starts(s,"ATREADY") || starts(s,"Call Ready") || starts(s,"SMS Ready") || strcmp(s,"RDY") == 0) {
        answer |= ANS_READY;
    }
    else if (starts(s,"+CSQ: ")) {
        int v = atoi(s + 6);
        network.csq = (v >= 0 && v <= 31) ? (uint8_t)v : 0xFF;
        answer |= ANS_CSQ;
    }
    else if (starts(s,"+CREG: ") || starts(s,"+CEREG: ")) {
        /* AT+CREG=2/AT+CEREG=2 make both the poll reply and the URC carry
           network.lac/ci:
             poll: <n>,<stat>[,"<lac/tac>","<ci>"]   URC: <stat>[,"<lac/tac>","<ci>"]
           Collect the unquoted leading comma-separated fields — the last
           one is always <stat> regardless of whether <n> is present.
           +CREG is the CS (GSM/UTRAN) domain; +CEREG is the PS/EPS (LTE)
           domain — a data-only LTE SIM only ever registers in the latter,
           so both are tracked (see csRegistered/epsRegistered's comment in
           Modem.h) and OR'd together below rather than one overwriting the
           other. */
        bool isCereg = starts(s,"+CEREG: ");
        const char* fieldsStart = s + (isCereg ? 8 : 7);
        const char* p = fieldsStart;
        char tok[2][8]; int ntok = 0;
        {
            char buf[8]; int bi = 0;
            while (*p && *p != '"' && ntok < 2) {
                if (*p == ',') {
                    buf[bi] = 0;
                    if (bi) { strncpy(tok[ntok], buf, sizeof(tok[0])-1); tok[ntok][sizeof(tok[0])-1] = 0; ntok++; }
                    bi = 0;
                } else if (bi < (int)sizeof(buf)-1) buf[bi++] = *p;
                p++;
            }
            if (bi && ntok < 2) { buf[bi] = 0; strncpy(tok[ntok], buf, sizeof(tok[0])-1); tok[ntok][sizeof(tok[0])-1] = 0; ntok++; }
        }
        char stat = (ntok > 0) ? tok[ntok-1][0] : '0';
        if (isCereg) { epsRegistered = (stat == '1'); epsRoaming = (stat == '5'); }
        else         { csRegistered  = (stat == '1'); csRoaming  = (stat == '5'); }
        network.isRegistered = csRegistered || epsRegistered;
        network.isRoaming    = csRoaming || epsRoaming;

        if (*p == '"') {
            char hexLac[8], hexCi[12];
            nthQuoted(fieldsStart, 0, hexLac, sizeof(hexLac));
            nthQuoted(fieldsStart, 1, hexCi,  sizeof(hexCi));
            network.lac    = (uint16_t)strtol(hexLac, NULL, 16);
            network.cellId = (uint32_t)strtol(hexCi,  NULL, 16);
        }
        answer |= isCereg ? ANS_CEREG : ANS_CREG;
    }
    else if (starts(s,"+COPS: ")) {
        /* AT+COPS=3,2 selects numeric format, so the 3rd field (oper) is a
           quoted MCC+MNC string, e.g. +COPS: 0,2,"25099",7 */
        char oper[8];
        nthQuoted(s + 7, 0, oper, sizeof(oper));
        if (oper[0]) {
            strncpy(network.operatorCode, oper, sizeof(network.operatorCode)-1); network.operatorCode[sizeof(network.operatorCode)-1] = 0;
            const char* name = findOperatorName(network.operatorCode);
            if (name) { strncpy(network.operatorName, name, sizeof(network.operatorName)-1); network.operatorName[sizeof(network.operatorName)-1] = 0; }
            else network.operatorName[0] = 0;
        }

        /* <AcT> is the optional 4th field, right after oper's closing quote:
           +COPS: <mode>,<format>,"<oper>"[,<AcT>] — plain (unquoted) integer,
           real network tech (0=GSM,2=UTRAN,7=E-UTRAN,...), sent to the panel
           as-is; it buckets it into 2G/3G/4G for display. */
        {
            const char* p = s + 7;
            int quotes = 0;
            while (*p && quotes < 2) { if (*p == '"') quotes++; p++; }
            network.networkAcT = (*p == ',') ? (uint8_t)strtol(p + 1, NULL, 10) : 0xFF;
        }
        answer |= ANS_COPS;
    }
    else if (starts(s,"+CGPADDR: ")) {
        /* +CGPADDR: 1,<addr> — cid is 1st field, IP is 2nd field. Some modems
           quote the address ("10.23.45.67"), this one doesn't (10.23.45.67) —
           handle both instead of relying on nthQuoted(). */
        const char* p = s + 10;
        while (*p && *p != ',') p++;   /* skip cid */
        if (*p == ',') p++;
        if (*p == '"') p++;            /* tolerate an optional quote */
        uint16_t i = 0;
        while (*p && *p != ',' && *p != '"' && i < sizeof(internet.ipAddress)-1) internet.ipAddress[i++] = *p++;
        internet.ipAddress[i] = 0;
        answer |= ANS_CGPADDR;
    }
    else if (starts(s,"+HTTPACTION: ")) {
        /* +HTTPACTION: <method>,<status>,<datalen> — status is 2nd field.
           datalen (3rd field) is only used by doOta() (to size firmware.crc16's
           one-shot read) — parsed here unconditionally anyway since it's free
           and doCheckInternet() just ignores http.dataLen. */
        const char* p = s + 13;
        while (*p && *p != ',') p++;
        http.status = (*p == ',') ? (uint16_t)atoi(p + 1) : 0;
        if (*p == ',') {
            p++;
            while (*p && *p != ',') p++;
            http.dataLen = (*p == ',') ? (uint16_t)atoi(p + 1) : 0;
        }
        answer |= ANS_HTTPACTION;
    }
    else if (starts(s,"+HTTPREAD: ")) {
        /* +HTTPREAD: <len> — confirmed on real hardware to be one chunk of a
           possibly multi-chunk asynchronous delivery (module caps each chunk
           around 1024 bytes), NOT tied to the "OK" that immediately follows
           the AT+HTTPREAD command itself (that OK only means "command
           accepted" — see the RawCapture comment in Modem.h). len==0 is the
           terminator: the whole read is done, no more chunks coming. Only
           arms the next chunk's capture if a step actually pre-armed
           rawCapture.dst; an unexpected/unsolicited one (shouldn't happen
           given the strictly serial state machine) is harmlessly ignored
           rather than diverting into a stale buffer. Clamped so a chunk
           bigger than what's left of rawCapture.cap (bug/corrupt response)
           can't overflow it — the caller's own total-length check after
           ANS_HTTPREAD_DONE catches the resulting mismatch and retries. */
        uint16_t len = (uint16_t)atoi(s + 11);
        http.dataLen = len;
        if (rawCapture.dst) {
            if (len == 0) {
                answer |= ANS_HTTPREAD_DONE;
            } else {
                uint16_t room = (rawCapture.got < rawCapture.cap) ? (uint16_t)(rawCapture.cap - rawCapture.got) : 0;
                rawCapture.chunkRemaining = (len <= room) ? len : room;
            }
        }
        answer |= ANS_HTTPREAD;
    }
    else if (starts(s,"+CMQTTCONNECT: ") || starts(s,"+CMQTTSUB: ") || starts(s,"+CMQTTPUB: ")) {
        /* All three share "<client_index>,<result>" — 0 = success */
        const char* p = s;
        while (*p && *p != ',') p++;
        mqttScratch.urcResult = (*p == ',') ? (uint8_t)atoi(p + 1) : 0xFF;
        answer |= ANS_MQTT_URC;
    }
    else if (starts(s,"+CMQTTCONNLOST: ")) {
        mqtt.connected = false;
    }
    else if (starts(s,"+CMQTTRXTOPIC: ")) {
        capture = CAP_MQTT_TOPIC;
    }
    else if (starts(s,"+CMQTTRXPAYLOAD: ")) {
        capture = CAP_MQTT_PAYLOAD;
    }
    else if (starts(s,"+CMTI: ")) {
        /* +CMTI: "SM",3 — only touches sms.notifySlot, never the live
           sms.slot doReadSms() might currently be mid-read on (see the field
           comments in Modem.h for why that distinction matters). */
        const char* p = s + 7;
        while (*p && *p != ',') p++;
        sms.notifySlot = (*p == ',') ? (uint8_t)(*(p+1) - '0') : 1;
        if (sms.notifySlot == 0) sms.notifySlot = 1;
        answer |= ANS_CMTI;
        sms.notifyPending = true;
    }
    else if (starts(s,"+CMGL: ")) {
        /* +CMGL: <idx>,"REC UNREAD",... — periodic fallback poll only (see
           ST_POLL_SMS_UNREAD); just need an index here, not the body/phone —
           the existing CMGR-based doReadSms() fetches those properly once
           doIdle() copies sms.notifySlot into sms.slot. If more than one
           unread message is sitting in storage, whichever index lands last
           just means the others get caught on a later poll after this one
           is read+deleted. */
        const char* p = s + 7;
        uint8_t idx = 0;
        while (*p >= '0' && *p <= '9') { idx = (uint8_t)(idx * 10 + (*p - '0')); p++; }
        if (idx > 0) { sms.notifySlot = idx; answer |= ANS_CMGL; }
    }
    else if (starts(s,"+CMGR:")) {
        /* +CMGR: "REC UNREAD","+79001234567",, ... — phone is 2nd quoted field */
        nthQuoted(s + 7, 1, network.cmgrPhone, sizeof(network.cmgrPhone));
        capture = CAP_CMGR_BODY;
        answer |= ANS_CMGR;
    }
    else if (starts(s,"+CMGS: ")) {
        answer |= ANS_CMGS;
    }
    else if (starts(s,"+ICCID: ")) {
        strncpy(network.iccid, s + 8, sizeof(network.iccid)-1); network.iccid[sizeof(network.iccid)-1] = 0;
        answer |= ANS_ICCID;
    }
    else if (starts(s,"+CNUM:")) {
        /* +CNUM: "","<number>",145 — own number is 2nd quoted field */
        nthQuoted(s + 7, 1, network.ownNumber, sizeof(network.ownNumber));
        answer |= ANS_CNUM;
    }
    else if (starts(s,"+CUSD:")) {
        char raw[128];
        nthQuoted(s + 7, 0, raw, sizeof(raw));

        static char decoded[128];
        decodeUcs2Hex(raw, decoded, sizeof(decoded));

        log_info("[USSD] "); log_info(decoded); log_info("\r\n");
        answer |= ANS_CUSD;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   State handlers
   ═══════════════════════════════════════════════════════════════════════*/

void Modem::doPowerOn(void) {
    static uint32_t t = 0;
    switch (step) {
    case 0:
        log_info("Modem: check\r\n");
        step++;
        break;
    case 1:
        if (atCmd("AT\r\n", 1000)) {
            if (answer & ANS_OK) { setState(ST_WAIT_READY); }
            else { PowerkeyPin.Set(); t = core.getTick(); step++; }
        }
        break;
    case 2:
        if ((core.getTick()-t) >= 1500) { PowerkeyPin.Reset(); t = core.getTick(); step++; }
        break;
    case 3:
        if ((core.getTick()-t) >= 500) setState(ST_WAIT_READY);
        break;
    }
}

void Modem::doWaitReady(void) {
    static uint32_t t = 0;
    static bool first = true;
    if (first) { first = false; t = core.getTick(); log_info("Modem: wait ready\r\n"); }
    if ((answer & ANS_READY) || (core.getTick()-t) >= 12000) { first = true; setState(ST_INIT); }
}

void Modem::doInit(void) {
    switch (step) {
    case 0:  if (atCmd("AT\r\n",                  3000)) step++; break;
    case 1:  if (atCmd("ATE0\r\n",                1000)) step++; break;
    case 2:  if (atCmd("AT+CMEE=2\r\n",            300)) step++; break;
    case 3:  if (atCmd("AT+CMGF=1\r\n",            500)) step++; break;
    case 4:  if (atCmd("AT+CMGD=1,4\r\n",          5000)) step++; break;
    case 5:  if (atCmd("AT+CNMI=2,1,0,0,0\r\n",    300)) step++; break;
    case 6:  if (atCmd("AT+CSQ\r\n",              1000)) step++; break;
    case 7:  if (atCmd("AT+CICCID\r\n",           5000)) step++; break;
    case 8:
        /* IMEI arrives as a plain line before OK — use capture mode */
        capture = CAP_IMEI;
        if (atCmd("AT+CGSN\r\n", 3000)) { capture = CAP_NONE; step++; }
        break;
    case 9:  if (atCmd("AT+CNUM\r\n",             3000)) step++; break;
    case 10: if (atCmd("AT+CREG=2\r\n",            300)) step++; break;
    case 11: if (atCmd("AT+CREG?\r\n",            2000)) step++; break;
    case 12: if (atCmd("AT+CEREG=2\r\n",           300)) step++; break;
    case 13: if (atCmd("AT+CEREG?\r\n",           2000)) step++; break;
    case 14: if (atCmd("AT+COPS=3,2\r\n",          300)) step++; break;
    case 15: if (atCmd("AT+COPS?\r\n",            3000)) step++; break;
    default:
        log_info("Modem ready. IMEI="); log_info(network.imei[0]       ? network.imei       : "?");
        log_info(" SIM=");              log_info(network.ownNumber[0]   ? network.ownNumber  : "?");
        log_info("\r\n");
        timers.csq = timers.creg = core.getTick();
        setState(config.useInternet ? ST_INIT_NET : ST_IDLE);
        break;
    }
}

void Modem::doIdle(void) {
    uint32_t now = core.getTick();

    /* React to config.useInternet being flipped at runtime (e.g. PGN60 write from a
       panel, or the "internet" SMS command), roaming status changing (CREG
       polling), or config.allowRoaming being flipped (the "roaming" SMS command) —
       without waiting for a modem reboot. internetAllowed folds all three
       into one condition so the same teardown/init handling covers all of
       them; seed from the current value on first entry (doInit() already
       handled the boot-time case). */
    bool internetAllowed = config.useInternet && (!network.isRoaming || config.allowRoaming);
    static bool first = true;
    static bool prevInternetAllowed;
    static bool prevForce2gOnly;
    if (first) { first = false; prevInternetAllowed = internetAllowed; prevForce2gOnly = config.force2gOnly; }
    if (prevInternetAllowed != internetAllowed) {
        prevInternetAllowed = internetAllowed;
        if (!internetAllowed) {
            internet.isInternetConnected = false;
            if (mqtt.connected) { mqttScratch.teardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }
            setState(ST_NET_TEARDOWN);
            return;
        } else {
            prevForce2gOnly = config.force2gOnly;  /* doInitNet() below reads it fresh anyway */
            setState(ST_INIT_NET);
            return;
        }
    }

    /* React to config.force2gOnly being flipped at runtime (PGN60 write from a
       panel, or the "2g" SMS command) while internet is already up — tear
       down and re-init the PDP context so the new AT+CNMP value actually
       takes effect now, instead of waiting for the next reboot/reconnect
       (doInitNet()'s step 0 only runs as part of that init sequence). Only
       meaningful while internetAllowed: if internet isn't up at all, the
       next time it comes up above already reads config.force2gOnly fresh. */
    if (internetAllowed && prevForce2gOnly != config.force2gOnly) {
        prevForce2gOnly = config.force2gOnly;
        internet.isInternetConnected = false;
        mqttScratch.netTeardownThenReinit = true;
        if (mqtt.connected) { mqttScratch.teardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }
        setState(ST_NET_TEARDOWN);
        return;
    } else if (!internetAllowed) {
        prevForce2gOnly = config.force2gOnly;
    }

    /* Internet dropped out from under an active MQTT session — tear it down;
       doCheckInternet()'s own self-heal will bring the PDP context back, and
       the retry timer below will restart MQTT once internet.isInternetConnected again. */
    if (mqtt.connected && !internet.isInternetConnected) { mqttScratch.teardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }

    /* mqttForceReconnect() was called (e.g. broker/password changed via SMS) —
       safe to act on now since doIdle() only ever runs from a clean top-level
       dispatch, never nested inside another state's handler. */
    if (mqttScratch.reconnectRequested) {
        mqttScratch.reconnectRequested = false;
        if (mqtt.connected) { mqttScratch.teardownThenNet = false; setState(ST_MQTT_TEARDOWN); return; }
        timers.mqttRetry = now - 5000;  /* let the retry check below fire immediately */
    }

    /* startOta() was called (e.g. from onMqttCommandReceived()) — same
       reentrancy rationale as mqttScratch.reconnectRequested above, only actually
       entered from doIdle()'s own clean top-level dispatch. Left pending
       (not cleared) if internet isn't up yet — doIdle() just re-checks this
       every pass until it is, same as the MQTT retry path below implicitly
       waiting on internet.isInternetConnected too. */
    if (otaScratch.startRequested && internet.isInternetConnected) {
        otaScratch.startRequested = false;
        ota.status = OTA_STAGING;
        ota.page = 0;
        otaScratch.failed = false;
        /* deviceType == VERSION_1: this modem's own firmware, not a
           CAN-relay target — skip ST_FETCH_PROFILE entirely (the profile/
           bootloaders.txt only exist to tell doCanRelay() a target's flash
           layout and safe bootloader algorithm; the self-OTA buffer has no
           CAN relay step) and go straight to the download. */
        setState(otaScratch.deviceType == VERSION_1 ? ST_OTA : ST_FETCH_PROFILE);
        return;
    }

    /* startAutoRegister() was called from Timberline's CAN dispatch — same
       reentrancy rationale as otaScratch.startRequested above, and same
       "leave pending until internet is up" behavior. */
    if (regScratch.startRequested && internet.isInternetConnected) {
        regScratch.startRequested = false;
        regScratch.retries = 0;
        autoRegisterStatus = AUTOREG_BUSY;
        setState(ST_AUTO_REGISTER);
        return;
    }

    if (sms.notifyPending) { sms.notifyPending = false; sms.slot = sms.notifySlot; setState(ST_READ_SMS); return; }

    /* Promote the oldest queued outgoing SMS into the active slot once it's
       free — safe here since doIdle() only ever runs from a clean top-level
       dispatch (see SmsQueueEntry comment in Modem.h). */
    if (!sms.pending) {
        for (int i = 0; i < SMS_QUEUE_MAX; i++) {
            if (sms.queue[i].pending) {
                strncpy(network.smsPhone, sms.queue[i].phone, sizeof(network.smsPhone) - 1); network.smsPhone[sizeof(network.smsPhone)-1] = 0;
                strncpy(network.smsText,  sms.queue[i].text,  sizeof(network.smsText)  - 1); network.smsText[sizeof(network.smsText) -1] = 0;
                sms.queue[i].pending = false;
                sms.pending = true;
                break;
            }
        }
    }
    if (sms.pending)                  { setState(ST_SEND_SMS);  return; }
    if (ussdPending)                 { setState(ST_USSD);       return; }
    if ((now - timers.csq)  >= 30000) { setState(ST_POLL_CSQ);   return; }
    /* Retry faster while not yet registered (e.g. right after a SIM swap or
       a coverage gap) so a missed first attempt doesn't cost a full minute —
       observed on real hardware: first CREG/COPS poll came back "no network
       service", and the fixed 60s cadence meant the SIM sat unregistered for
       almost a minute before the next try even though it would have found
       the network much sooner. Once registered, back off to the normal 60s
       so we're not needlessly polling the air interface. */
    uint32_t cregInterval = network.isRegistered ? 60000 : 5000;
    if ((now - timers.creg) >= cregInterval) { setState(ST_POLL_CREG);  return; }
    if (internetAllowed && (now - timers.net) >= 60000) {
        /* Already up — just re-verify (cheap: CGPADDR ± HTTP check). Down —
           go through the full CNMP/CGDCONT/CGATT/CGACT sequence again
           (ST_CHECK_INTERNET alone only re-checks CGPADDR; it can't bring a
           torn-down PDP context back up by itself). */
        setState(internet.isInternetConnected ? ST_CHECK_INTERNET : ST_INIT_NET);
        return;
    }

    /* Safety net, not the primary path (that's sms.notifyPending, set
       directly off the +CMTI: URC) — every couple of minutes, ask the SIM
       directly whether anything unread is sitting in storage that we
       somehow never got notified about. Rare/cheap enough not to matter;
       see doPollSmsUnread(). */
    if ((now - timers.smsPoll) >= 120000) { setState(ST_POLL_SMS_UNREAD); return; }

    /* No point even trying without all three — CMQTTACCQ/CMQTTCONNECT just
       fail immediately on an empty client id/host anyway (confirmed on real
       hardware: "" client id gets a flat ERROR from the module, empty host
       gives a "tcp://:1883" URL that can never connect), so this would
       otherwise retry every 30s forever on a device that's simply never
       been configured yet — wasted airtime/CPU and, worse, extra time spent
       off ST_IDLE for no possible benefit. */
    bool mqttConfigured = mqtt.broker[0] && mqtt.username[0] && mqtt.password[0];

    /* Flat 5s retry, no special-casing for "just came up" vs "retry after a
       failed attempt" (2026-08-21, per explicit request — earlier attempts to
       tune this delay at 45s/30s/10s all missed: real-hardware logs showed
       CMQTTSTART erroring on the first attempt regardless of how long we'd
       already waited, so no delay length here was ever going to fix it —
       the actual problem was elsewhere (see doMqttStart's CMQTTSTOP-first
       fix). Simplest thing that works: just try again every 5s. */
    if (internetAllowed && internet.isInternetConnected && !mqtt.connected && mqttConfigured &&
        (now - timers.mqttRetry) >= 5000) {
        timers.mqttRetry = now;
        setState(ST_MQTT_START);
        return;
    }
    if (mqtt.connected && mqttQueueHasPending()) { setState(ST_MQTT_PUB); return; }
}

/* ── sendUssd ────────────────────────────────────────────────────────── */
void Modem::sendUssd(const char* req) {
    if (!req || !req[0] || ussdPending) return;
    strncpy(ussdReq, req, sizeof(ussdReq) - 1);
    ussdReq[sizeof(ussdReq) - 1] = 0;
    /* USSD codes always end with '#'; drop any junk the terminal appended */
    char* last_hash = strrchr(ussdReq, '#');
    if (last_hash) *(last_hash + 1) = '\0';
    if (!ussdReq[0]) return;
    ussdPending = true;
}

/* ── doUssd ──────────────────────────────────────────────────────────── */
void Modem::doUssd(void) {
    static char cmd[48];

    switch (step) {
    case 0: {
        /* A7682 firmware rejects '#' in USSD strings (CME ERROR).
           Send without the trailing '#' — tested: network still responds. */
        int n = 0;
        const char* pre = "AT+CUSD=1,\"";
        while (*pre) cmd[n++] = *pre++;
        for (int i = 0; ussdReq[i] && ussdReq[i] != '#' && n < 44; i++)
            cmd[n++] = ussdReq[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;

        if (atCmd(cmd, 15000)) {
            if (answer & ANS_TIMEOUT) log_info("[USSD] timeout\r\n");
            if (answer & ANS_ERROR)   log_info("[USSD] error\r\n");
            ussdPending = false;
            setState(ST_IDLE);
        }
        break;
    }
    }
}

void Modem::doReadSms(void) {
    static char cmd[16];

    switch (step) {
    case 0:
        if (atCmd("AT+CMGF=1\r\n", 500)) step++;
        break;
    case 1: {
        /* Build "AT+CMGR=N\r\n" for slot 1-9 */
        int n = 0;
        const char* pre = "AT+CMGR=";
        while (*pre) cmd[n++] = *pre++;
        if (sms.slot >= 10) cmd[n++] = (char)('0' + sms.slot / 10);
        cmd[n++] = (char)('0' + sms.slot % 10);
        cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;

        if (atCmd(cmd, 5000)) {
            if ((answer & ANS_CMGR) && (answer & ANS_OK)) {
                log_info("SMS from "); log_info(network.cmgrPhone); log_info(": "); log_info(network.cmgrBody); log_info("\r\n");
                if (onSmsReceived) onSmsReceived(network.cmgrPhone, network.cmgrBody);
            }
            sms.slot = 0;
            step++;
        }
        break;
    }
    case 2:
        if (atCmd("AT+CMGD=1,2\r\n", 5000)) setState(ST_IDLE);
        break;
    }
}

void Modem::doSendSms(void) {
    static char cmd[36];
    static uint32_t t = 0;
    static uint8_t retries = 0;

    switch (step) {
    case 0:
        retries = 0;
        if (atCmd("AT+CSCS=\"IRA\"\r\n", 300)) step++;
        break;
    case 1:
        if (atCmd("AT+CMGF=1\r\n", 500)) step++;
        break;
    case 2: {
        /* Build AT+CMGS="+PHONE"\r\n */
        int n = 0;
        const char* pre = "AT+CMGS=\"";
        while (*pre) cmd[n++] = *pre++;
        for (int i = 0; network.smsPhone[i] && n < 33; i++) cmd[n++] = network.smsPhone[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;

        if (atCmd(cmd, 3000)) {
            if (answer & ANS_PROMPT) step++;
            else { sms.pending = false; setState(ST_IDLE); }
        }
        break;
    }
    case 3: {
        /* Send body + CTRL-Z in one call — two separate transmit() calls
           would let the second overwrite the TX buffer before the first finishes */
        static char bodyBuf[143];
        uint8_t n = 0;
        while (network.smsText[n] && n < 140) { bodyBuf[n] = network.smsText[n]; n++; }
        bodyBuf[n++] = '\x1A';
        bodyBuf[n]   = 0;
        answer = 0;
        t = core.getTick();
        transmit(bodyBuf);
        step++;
        break;
    }
    case 4:
        drainRx();
        if (answer & ANS_OK) {
            sms.pending = false;
            setState(ST_IDLE);
        } else if (answer & ANS_ERROR) {
            /* CMGS occasionally fails right after MQTT connects and starts
               publishing heavily — confirmed on real hardware ("+CMS ERROR:
               unknown error" on every send attempt while MQTT was actively
               busy, none before it connected). Likely the module's own
               CS/PS contention, not a firmware logic bug — but this is a
               user-facing confirmation reply, worth retrying a few times
               rather than dropping it silently the way a single ANS_ERROR
               used to.

               Loop back through case 1 (AT+CMGF=1), not straight back to
               case 2 — atCmd() dedupes by comparing the command string's
               hash against the last one it sent; re-issuing the exact same
               AT+CMGS="..." right after a failure would just replay the
               stale ANS_ERROR instead of actually retransmitting, since as
               far as atCmd() can tell nothing changed. Routing through a
               genuinely different command first forces a real resend when
               we get back to case 2. */
            if (retries < 3) {
                retries++;
                step = 1;
            } else {
                sms.pending = false;
                setState(ST_IDLE);
            }
        } else if ((core.getTick()-t) >= 10000) {
            sms.pending = false;
            setState(ST_IDLE);
        }
        break;
    }
}

void Modem::doPollCsq(void) {
    if (atCmd("AT+CSQ\r\n", 1000)) { timers.csq = core.getTick(); setState(ST_IDLE); }
}

void Modem::doPollCreg(void) {
    /* AT+CREG=2/AT+CEREG=2 enable network.lac/ci in the +CREG:/+CEREG: response
       (and URC); cheap to re-assert every poll since some modems forget it
       across power cycles. Both domains are polled — see the +CREG:/+CEREG:
       handler in parseLine() for why a data-only LTE SIM needs +CEREG too.
       AT+COPS=3,2 selects numeric operator format for the +COPS: query. */
    switch (step) {
    case 0: if (atCmd("AT+CREG=2\r\n",   300)) step++; break;
    case 1: if (atCmd("AT+CREG?\r\n",   2000)) step++; break;
    case 2: if (atCmd("AT+CEREG=2\r\n",  300)) step++; break;
    case 3: if (atCmd("AT+CEREG?\r\n",  2000)) step++; break;
    case 4: if (atCmd("AT+COPS=3,2\r\n", 300)) step++; break;
    case 5: if (atCmd("AT+COPS?\r\n",  3000)) step++; break;
    default:
        timers.creg = core.getTick();
        setState(ST_IDLE);
        break;
    }
}

void Modem::doPollSmsUnread(void) {
    /* Safety net for the CMTI-loss scenario sms.notifyPending already covers —
       in case a notification is ever missed for some other reason (garbled
       URC, module quirk), ask the SIM directly whether anything unread is
       still sitting in storage instead of relying solely on the module
       telling us proactively. AT+CMGF=1 re-asserted defensively, same as
       doReadSms() — cheap, and this runs rarely enough not to matter. */
    switch (step) {
    case 0: if (atCmd("AT+CMGF=1\r\n", 500)) step++; break;
    case 1:
        if (atCmd("AT+CMGL=\"REC UNREAD\"\r\n", 5000)) {
            /* answer is read here, before setState() below clears it. Just
               routes into the existing sms.notifyPending -> ST_READ_SMS ->
               doReadSms() path — that already fetches the message properly
               by index and cleans up read messages via CMGD=1,2. */
            if (answer & ANS_CMGL) sms.notifyPending = true;
            timers.smsPoll = core.getTick();
            setState(ST_IDLE);
        }
        break;
    }
}

/* ── Mobile internet (active only when config.useInternet) ──────────────────────── */

void Modem::doInitNet(void) {
    /* Bring up a PDP context on whatever radio access is available — auto
       2G/4G (AT+CNMP=2) normally, or GSM-only (AT+CNMP=13) if config.force2gOnly
       is set (see the "2g" SMS command). APN is blank (network-assigned
       default) unless the "apn" SMS command set an explicit override, or
       the operator is recognized (see case 1) — most SIMs this device uses
       don't need one. Errors/timeouts on any step don't block the sequence
       (same policy as doInit()); the real result is confirmed afterwards by
       doCheckInternet(). CNMP values are module-specific (A7682E); verified
       against its AT command set, not yet against real hardware. */
    static char cmd[96];

    /* Deutsche Telekom Germany (MCC/MNC 26201) is the only operator with a
       baked-in guess right now — its commonly documented defaults are APN
       "internet.telekom", username "telekom", password "tm", but none of
       this is confirmed yet against this exact SIM/module combo. Any of the
       three can be overridden individually via the "apn"/"apnuser"/"apnpass"
       SMS commands if the guess turns out wrong; every other operator keeps
       the original blank-APN, no-auth behavior, unchanged. */
    bool isTelekomDe = !strcmp(network.operatorCode, "26201");
    const char* apnToUse  = internet.apn[0]         ? internet.apn         : (isTelekomDe ? "internet.telekom" : "");
    const char* userToUse = internet.apnUsername[0] ? internet.apnUsername : (isTelekomDe ? "telekom"          : "");
    const char* passToUse = internet.apnPassword[0] ? internet.apnPassword : (isTelekomDe ? "tm"                : "");

    switch (step) {
    case 0: if (atCmd(config.force2gOnly ? "AT+CNMP=13\r\n" : "AT+CNMP=2\r\n", 300)) step++; break;
    case 1: {
        int n = 0;
        const char* pre = "AT+CGDCONT=1,\"IP\",\"";
        while (*pre) cmd[n++] = *pre++;
        for (int i = 0; apnToUse[i] && n < 58; i++) cmd[n++] = apnToUse[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 300)) step++;
        break;
    }
    case 2:
        if (!userToUse[0]) { step++; break; } /* no auth needed — skip CGAUTH entirely */
        {
            /* AT+CGAUTH=<cid>,<auth_type>,<user>,<pass> — auth_type 3 = "PAP
               or CHAP", lets the module/network settle it rather than
               guessing which one is actually required. */
            int n = 0;
            const char* pre = "AT+CGAUTH=1,3,\"";
            while (*pre) cmd[n++] = *pre++;
            for (int i = 0; userToUse[i] && n < 40; i++) cmd[n++] = userToUse[i];
            cmd[n++] = '"'; cmd[n++] = ','; cmd[n++] = '"';
            for (int i = 0; passToUse[i] && n < 88; i++) cmd[n++] = passToUse[i];
            cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
            if (atCmd(cmd, 300)) step++;
        }
        break;
    case 3: if (atCmd("AT+CGATT=1\r\n",               10000)) step++; break;
    case 4:
        if (atCmd("AT+CGACT=1,1\r\n", 15000))
            step = (answer & ANS_ERROR) ? 5 : 6;
        break;
    case 5:
        /* CGACT failed — AT+CEER reports the real reject cause instead of a
           bare ERROR; purely diagnostic; doesn't change control flow. It's
           logged automatically via the AT transcript (drainRx echoes every
           raw line), no separate parsing needed. */
        if (atCmd("AT+CEER\r\n", 2000)) step = 6;
        break;
    default:
        timers.net = core.getTick();
        setState(ST_CHECK_INTERNET);
        break;
    }
}

void Modem::doCheckInternet(void) {
    static char    cmd[96];
    static uint32_t t = 0;

    switch (step) {
    case 0:
        if (atCmd("AT+CGPADDR=1\r\n", 3000)) {
            /* Only trust internet.ipAddress if +CGPADDR: actually arrived this round —
               on a timeout/error it would otherwise still hold a stale value
               from a previous successful check. */
            bool hasIp = (answer & ANS_CGPADDR) && internet.ipAddress[0]
                       && strcmp(internet.ipAddress, "0.0.0.0") != 0;
            if (!hasIp) {
                /* PDP context is down. Used to self-heal by jumping straight
                   back into ST_INIT_NET — but that bypasses ST_IDLE forever
                   while the network keeps rejecting CGACT (e.g. roaming with
                   config.allowRoaming=false: the network correctly refuses data
                   attach every time, and this node would hammer AT+CGACT in
                   a tight loop indefinitely). ST_IDLE is the only place that
                   re-checks internetAllowed (see doIdle()) and CREG/CSQ get
                   polled — going there first lets a no-longer-allowed
                   condition (roaming, config.useInternet toggled off, ...) actually
                   stop the retries instead of being permanently starved.
                   timers.net is refreshed here so doIdle()'s 60s gate paces
                   from *this* failure, not some earlier stale mark — without
                   it the gate could read as already-expired and refire on
                   every doIdle() pass instead of waiting a real 60s. doIdle()
                   itself now routes back into ST_INIT_NET (not straight into
                   ST_CHECK_INTERNET) when internet.isInternetConnected is false, so
                   the PDP context actually gets re-provisioned instead of
                   just being re-polled forever. */
                internet.isInternetConnected = false;
                timers.net = core.getTick();
                setState(ST_IDLE);
                return;
            }
            if (!internet.internetCheckUrl[0]) {
                /* No check URL configured yet — PDP up is the best signal we have */
                if (!internet.isInternetConnected) {
                    /* First time up (not just re-confirming on a later 60s
                       poll) — see the case 5 comment below for why this
                       matters. */
                    timers.mqttRetry = core.getTick();
                }
                internet.isInternetConnected = true;
                timers.net = core.getTick();
                setState(ST_IDLE);
                return;
            }
            step++;
        }
        break;
    case 1: if (atCmd("AT+HTTPINIT\r\n",         2000)) step++; break;
    case 2: {
        /* No AT+HTTPPARA="CID",... — this modem rejects it (ERROR) and uses
           whatever PDP context is already active regardless. */
        int n = 0;
        const char* pre = "AT+HTTPPARA=\"URL\",\"";
        while (*pre) cmd[n++] = *pre++;
        for (int i = 0; internet.internetCheckUrl[i] && n < 90; i++) cmd[n++] = internet.internetCheckUrl[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 3:
        /* This only confirms the command was accepted — the actual result
           arrives later as an asynchronous +HTTPACTION: URC (case 4). */
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) { http.status = 0; step = 5; }
            else { answer &= ~ANS_HTTPACTION; t = core.getTick(); step++; }
        }
        break;
    case 4:
        if (answer & ANS_HTTPACTION) step++;
        else if ((core.getTick() - t) >= 20000) { http.status = 0; step++; }
        break;
    case 5:
        if (atCmd("AT+HTTPTERM\r\n", 2000)) {
            /* timers.mqttRetry starts at 0 (see the constructor) and doIdle()
               gates MQTT connect attempts on (now - timers.mqttRetry) >= 5000
               — without resetting it here, that window would count from raw
               power-on instead of from when the PDP context actually came
               up, so the very first attempt could fire before internet was
               even reachable. Only on the false->true transition — a later
               periodic re-check succeeding again shouldn't push out a
               pending MQTT retry after an unrelated MQTT-side failure. */
            bool wasConnected = internet.isInternetConnected;
            internet.isInternetConnected = (http.status > 0 && http.status < 400);
            if (!wasConnected && internet.isInternetConnected) timers.mqttRetry = core.getTick();
            timers.net = core.getTick();
            setState(ST_IDLE);
        }
        break;
    }
}

void Modem::doNetTeardown(void) {
    if (atCmd("AT+CGACT=0,1\r\n", 15000)) {
        bool reinit = mqttScratch.netTeardownThenReinit;
        mqttScratch.netTeardownThenReinit = false;
        setState(reinit ? ST_INIT_NET : ST_IDLE);
    }
}

/* ── MQTT control channel (active only when config.useInternet && internet.isInternetConnected) ── */

/* Enqueue "<mqtt.username>/cmd/actual/<name>" = payload for the next doMqttPub() pass.
   Coalescing: a name already queued just gets its value overwritten — only the
   latest value per name matters, there's no point re-sending stale intermediate
   ones. Safe to call regardless of mqtt.connected; it'll drain once connected. */
void Modem::mqttPublish(const char* name, const char* payload) {
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++) {
        if (mqttScratch.pubQueue[i].name[0] && !strcmp(mqttScratch.pubQueue[i].name, name)) {
            strncpy(mqttScratch.pubQueue[i].value, payload, sizeof(mqttScratch.pubQueue[i].value)-1);
            mqttScratch.pubQueue[i].value[sizeof(mqttScratch.pubQueue[i].value)-1] = 0;
            mqttScratch.pubQueue[i].dirty = true;
            return;
        }
    }
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++) {
        if (!mqttScratch.pubQueue[i].name[0]) {
            strncpy(mqttScratch.pubQueue[i].name, name, sizeof(mqttScratch.pubQueue[i].name)-1);
            mqttScratch.pubQueue[i].name[sizeof(mqttScratch.pubQueue[i].name)-1] = 0;
            strncpy(mqttScratch.pubQueue[i].value, payload, sizeof(mqttScratch.pubQueue[i].value)-1);
            mqttScratch.pubQueue[i].value[sizeof(mqttScratch.pubQueue[i].value)-1] = 0;
            mqttScratch.pubQueue[i].dirty = true;
            return;
        }
    }
    /* queue full (all MQTT_PUB_MAX names in use) — drop silently */
}

/* Lightweight, non-cryptographic token generator — this MCU has no hardware
   RNG. Mixes the boot tick, a call counter and the device's own IMEI through
   two rounds of a simple string hash so repeated calls produce different
   values. Not resistant to a determined attacker who can guess timing —
   accepted as a known limitation. Used for the "getlink" token itself, and
   (via publishLinkToken()) reused as-is for auto-registration's generated
   login/password — same non-guarantee applies there too, but a self-chosen
   throwaway MQTT password doesn't need to be cryptographically strong, just
   not predictable at a glance. */
/* Shared randomness core for generateLinkToken()/generateMemorableLogin() —
   mixes the boot tick, a call counter and the device's own IMEI through two
   rounds of a simple string hash, so repeated calls (even within the same
   millisecond) produce different (h1,h2) pairs. */
static void generateRandomPair(uint32_t& h1, uint32_t& h2) {
    static uint32_t counter = 0;
    counter++;

    char mix[32];
    int  n = 0;
    uint32_t tick = core.getTick();
    for (int i = 0; i < 4; i++) mix[n++] = (char)(tick >> (i * 8));
    for (int i = 0; i < 4; i++) mix[n++] = (char)(counter >> (i * 8));
    for (int i = 0; modem.network.imei[i] && n < 30; i++) mix[n++] = modem.network.imei[i];

    h1 = 5381; h2 = 52711;
    for (int i = 0; i < n; i++) {
        h1 = h1 * 33 ^ (uint8_t)mix[i];
        h2 = h2 * 33 ^ (uint8_t)mix[n - 1 - i];
    }
}

/* Lightweight, non-cryptographic token generator — this MCU has no hardware
   RNG. Not resistant to a determined attacker who can guess timing —
   accepted as a known limitation. Used for the "getlink" token itself, and
   (via publishLinkToken()) for auto-registration's generated password —
   same non-guarantee applies there too, but a self-chosen throwaway MQTT
   password doesn't need to be cryptographically strong, just not
   predictable at a glance. */
static void generateLinkToken(char* out, int outLen) {
    uint32_t h1, h2;
    generateRandomPair(h1, h2);

    static const char hex[] = "0123456789abcdef";
    int p = 0;
    for (int i = 7; i >= 0 && p < outLen - 1; i--) out[p++] = hex[(h1 >> (i * 4)) & 0xF];
    for (int i = 7; i >= 0 && p < outLen - 1; i--) out[p++] = hex[(h2 >> (i * 4)) & 0xF];
    out[p] = '\0';
}

/* Auto-registration's login: "<word><number>" (e.g. "fox228", "bell1337")
   instead of a hex blob — same non-strength caveat as generateLinkToken()
   (not the account's real secret, just its public-facing name; the
   generated password carries the actual entropy), picked to be short enough
   to read off the QR screen and remember/say out loud if ever needed. Word
   picked by h1 % word count, a 0-9999 number by h2 % 10000 (matches the
   requested "fox228"/"bell1337"/"core322" shape — 1-4 digits, no reason to
   exclude short numbers like "wolf22"). */
static void generateMemorableLogin(char* out, int outLen) {
    static const char* const kWords[] = {
        "fox","bell","core","wolf","lynx","hawk","puma","bear","deer","seal",
        "storm","cloud","flame","spark","ember","frost","drift","blaze","comet","orbit",
        "pixel","cyber","nexus","byte","chip","node","wave","beam","gate","tower",
        "otter","hare","lion","tiger","zebra","camel","goat","swan","owl","eagle",
        "raven","heron","robin","finch","quail","ibis","crane","gecko","viper","cobra",
        "shark","whale","orca","walrus","badger","ferret","weasel","marten","jackal","coyote",
        "panda","koala","sloth","lemur","gibbon","macaw","toucan","condor","falcon","osprey",
        "stork","egret","plover","curlew","grouse","pigeon","sparrow","swift","martin","linnet",
        "canyon","fjord","grove","marsh","swamp","tundra","valley","summit","glacier","lagoon",
        "harbor","island","desert","oasis","plateau","volcano","geyser","cavern","cape","strait",
        "gorge","meadow","prairie","bluff","knoll","hollow","thicket","copse","estuary","atoll",
        "thunder","breeze","gale","mist","haze","dew","rain","snow","hail","sleet",
        "gust","squall","zephyr","aurora","tempest","cyclone","monsoon","drizzle","shower","chinook",
        "rocket","probe","drone","laser","radar","sonar","pulse","vector","matrix","cipher",
        "relay","signal","beacon","module","circuit","vertex","zenith","crest","spire","dome",
        "arch","vault","anchor","compass","nova","quark","photon","proton","plasma","fusion",
        "amber","onyx","opal","jade","ruby","topaz","coral","pearl","slate","flint",
        "quartz","granite","marble","copper","silver","bronze","steel","iron","zinc","cobalt",
        "fern","moss","pine","oak","elm","birch","cedar","maple","willow","thorn",
        "bloom","petal","leaf","root","vine","reed","bramble","clover","ivy","sage",
    };
    const int kWordCount = (int)(sizeof(kWords) / sizeof(kWords[0]));

    uint32_t h1, h2;
    generateRandomPair(h1, h2);

    const char* word = kWords[h1 % kWordCount];
    uint32_t number = h2 % 10000;  /* 0-9999 — no reason to exclude 0-99, "wolf22" is fine */

    int p = 0;
    for (const char* c = word; *c && p < outLen - 1; c++) out[p++] = *c;
    p = appendUint(out, p, number);
    if (p > outLen - 1) p = outLen - 1;
    out[p] = '\0';
}

/* Shared finishing step for "getlink" (SMS) and a just-succeeded
   auto-registration: generate a fresh token, publish it retained, and build
   the https://<broker>/go/<username>/<token> URL directly into
   internet.connectionLink. Requires mqtt.username/broker already set —
   auto-registration sets them just before calling this (see
   doAutoRegister()). */
void Modem::publishLinkToken(void) {
    char token[17];
    generateLinkToken(token, sizeof(token));
    mqttPublish("linkToken", token);

    char* url = internet.connectionLink;
    int   n = 0;
    const int urlMax = (int)sizeof(internet.connectionLink) - 1;
    /* https on the default port (443, terminated by nginx in front of the
       web app) — not ":3000", which is the app's own plain-HTTP port and has
       no TLS cert bound to it. Also requires mqtt.broker to hold a hostname
       (matching the cert's CN), not a bare IP. */
    const char* pre = "https://";
    while (*pre) url[n++] = *pre++;
    for (const char* p = mqtt.broker; *p && n < urlMax; ) url[n++] = *p++;
    const char* mid = "/go/";
    while (*mid && n < urlMax) url[n++] = *mid++;
    for (const char* p = mqtt.username; *p && n < urlMax; ) url[n++] = *p++;
    if (n < urlMax) url[n++] = '/';
    for (const char* p = token; *p && n < urlMax; ) url[n++] = *p++;
    url[n] = '\0';
}

bool Modem::mqttQueueHasPending(void) {
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++)
        if (mqttScratch.pubQueue[i].dirty) return true;
    return false;
}

/* Call after changing mqtt.broker/mqtt.username/mqtt.password at runtime (e.g.
   the "server"/"password" SMS commands) so the new values take effect right
   away instead of waiting for the connection to drop on its own. Only sets a
   flag — see the mqttScratch.reconnectRequested comment in Modem.h for why this
   can't safely call setState() directly (may be invoked re-entrantly from
   inside onSmsReceived, itself called from doReadSms()). */
void Modem::mqttForceReconnect(void) {
    mqttScratch.reconnectRequested = true;
}

void Modem::doMqttStart(void) {
    static char     cmdBuf[144];
    static char     willTopicBuf[48];
    static uint32_t t = 0;
    const  int      cmdMax = (int)sizeof(cmdBuf) - 1;

    switch (step) {
    case 0:
        /* Unconditional AT+CMQTTDISC/REL/STOP before every start attempt,
           including the very first one after boot. Root cause confirmed via
           SIMCOM's own CMQTT error table (2026-08-21): the module answers
           CMQTTSTART/CMQTTSTOP with error 19, "client is used" — client
           index 0's allocation survives independently of this MCU's own
           reset/reflash, since the cellular module itself doesn't reboot
           with it. So any run where the *previous* firmware session ended
           without a clean CMQTTREL (crash, reflash, power cut mid-session)
           leaves index 0 marked used from the module's point of view, and a
           bare CMQTTSTOP doesn't clear that flag — only CMQTTREL does, which
           is why only the *second* attempt (run through this same
           DISC/REL/STOP sequence in the `default:` failure path below) ever
           used to succeed. Running it up front here avoids that first,
           otherwise near-guaranteed, failed round-trip. All three are
           harmless no-ops (ERROR, ignored — atCmd() advances regardless)
           when there was nothing to disconnect/release/stop. */
        if (atCmd("AT+CMQTTDISC=0,60\r\n", 5000)) step++;
        break;
    case 1: if (atCmd("AT+CMQTTREL=0\r\n", 3000)) step++; break;
    case 2: if (atCmd("AT+CMQTTSTOP\r\n", 3000)) step++; break;
    case 3: if (atCmd("AT+CMQTTSTART\r\n", 5000)) step++; break;
    case 4: {
        int n = 0;
        const char* pre = "AT+CMQTTACCQ=0,\"";
        while (*pre) cmdBuf[n++] = *pre++;
        for (const char* p = mqtt.username; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        cmdBuf[n++] = '"'; cmdBuf[n++] = '\r'; cmdBuf[n++] = '\n'; cmdBuf[n] = 0;
        if (atCmd(cmdBuf, 3000)) step++;
        break;
    }
    /* Last Will and Testament — "<username>/cmd/actual/online" = "0",
       registered with the broker before CONNECT so that if this session
       ever drops without a clean DISCONNECT (crashed, lost signal, power
       loss — not caught by anything else in this firmware), the broker
       publishes it on our behalf. See mqtt-topic-scheme memory for why
       this beats a polled timestamp. Length-prefixed-then-raw-bytes, same
       proven pattern as CMQTTTOPIC/CMQTTPAYLOAD in doMqttPub() below — NOT
       verified against real hardware yet, unlike the rest of this
       sequence; if it errors out here, CMQTTCONNECT below still runs
       regardless (atCmd() advances on ERROR same as OK), just without a
       registered will. */
    case 5: {
        int n = 0;
        const int topicMax = (int)sizeof(willTopicBuf) - 1;
        for (const char* p = mqtt.username; *p && n < topicMax; ) willTopicBuf[n++] = *p++;
        const char* mid = "/cmd/actual/online";
        for (const char* p = mid; *p && n < topicMax; ) willTopicBuf[n++] = *p++;
        willTopicBuf[n] = 0;

        int cn = 0;
        const char* pre = "AT+CMQTTWILLTOPIC=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) step++;
        break;
    }
    case 6:
        if (atCmd(willTopicBuf, 3000)) step++;
        break;
    case 7:
        /* len=1, qos=1, retain=1 — without retain, a client that (re)subscribes
           after the will already fired just gets the stale last retained "1"
           instead of the will's "0", masking the outage for anyone whose
           connection wasn't live at the exact moment the will published. */
        if (atCmd("AT+CMQTTWILLMSG=0,1,1,1\r\n", 2000)) step++;
        break;
    case 8:
        if (atCmd("0", 3000)) step++;
        break;
    case 9: {
        int n = 0;
        const char* pre = "AT+CMQTTCONNECT=0,\"tcp://";
        while (*pre) cmdBuf[n++] = *pre++;
        for (const char* p = mqtt.broker; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        /* Keepalive 25s, not 60 — confirmed on real hardware (2026-08-21):
           mosquitto's own log showed this client's source IP cycling between
           exactly two addresses (the operator's small CGNAT pool) with an
           almost perfectly regular ~44-45s period, matching CMQTTCONNLOST
           events one-for-one. That's the signature of a carrier NAT/firewall
           killing an idle TCP mapping before our old 60s keepalive's first
           PING would ever go out — the module never gets a chance to keep
           the binding alive. 25s gives a solid margin under that ~45s
           window so PINGs refresh the NAT binding well before it can expire,
           on 2G/EDGE as much as 4G (attach type doesn't change the
           carrier-side NAT timeout that's actually the culprit here). */
        const char* mid = ":1883\",25,1,\"";
        while (*mid) cmdBuf[n++] = *mid++;
        for (const char* p = mqtt.username; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        cmdBuf[n++] = '"'; cmdBuf[n++] = ','; cmdBuf[n++] = '"';
        for (const char* p = mqtt.password; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        cmdBuf[n++] = '"'; cmdBuf[n++] = '\r'; cmdBuf[n++] = '\n'; cmdBuf[n] = 0;
        /* No "answer &= ~ANS_MQTT_URC" here on purpose (removed 2026-08-21) —
           atCmd() already zeroes `answer` fresh the moment this distinct
           command is transmitted (see atCmd()'s h != h0 branch), so there's
           nothing stale to clear. Confirmed on real hardware this clear was
           actively destroying real data: on a synchronous failure, the
           module returns "+CMQTTCONNECT: 0,19" (setting ANS_MQTT_URC) on the
           very same line burst as the command's own ERROR — clearing it
           right as atCmd() completes threw away that already-arrived result,
           forcing case 10 below to sit out the full 20s timeout for a URC
           that had, in fact, already shown up. */
        if (atCmd(cmdBuf, 3000)) { t = core.getTick(); step++; }
        break;
    }
    case 10:
        /* AT+CMQTTCONNECT is accepted immediately (OK); the real result
           usually arrives later as an asynchronous +CMQTTCONNECT: URC, but
           on a synchronous failure it can also arrive inline with the
           command's own ERROR (see the note above) — either way, answer
           already reflects it by the time we get here. */
        if (answer & ANS_MQTT_URC) step++;
        else if ((core.getTick() - t) >= 20000) { mqttScratch.urcResult = 0xFF; step++; }
        break;
    default:
        if (mqttScratch.urcResult == 0) {
            /* Set true here, not only in doMqttSub()'s own success case —
               confirmed on real hardware (2026-08-22) that leaving it false
               until SUB succeeds made doMqttSub()'s own CONNLOST bail-out
               guard ("if (!mqtt.connected) return to ST_IDLE", added to
               react to a disconnect mid-subscribe) trigger on *every* fresh
               connect instead: connected was still false the first time
               doMqttSub() ran, so it bailed immediately without ever
               sending CMQTTSUB, leaving connected permanently false and the
               45s/5s retry gate in doIdle() re-triggering a full teardown+
               reconnect forever, in a tight loop — CMQTTCONNECT kept
               succeeding, but the modem never once actually subscribed.
               Setting it true right when the connection itself succeeds
               (which is what it actually means) makes that guard correctly
               distinguish "just connected, about to subscribe" from "was
               connected, then lost it mid-subscribe". */
            mqtt.connected = true;
            mqttPublish("online", "1");  /* announce ourselves once connected/subscribed */
            setState(ST_MQTT_SUB);
        } else {
            /* Connect failed — reset the modem's MQTT client state (DISC/REL/STOP)
               before retrying, otherwise the next CMQTTSTART/ACCQ/CONNECT just
               errors out forever because the old session is still considered live. */
            mqttScratch.teardownThenNet = false;
            setState(ST_MQTT_TEARDOWN);
        }
        break;
    }
}

void Modem::doMqttSub(void) {
    static char     topicBuf[48];
    static char     cmdBuf[32];
    static uint32_t t = 0;

    /* +CMQTTCONNLOST can land at any moment (parseLine() sets mqtt.connected
       false as soon as it's parsed, asynchronously to whatever step this
       function happens to be on) — bail out immediately instead of grinding
       through the remaining AT+CMQTTSUB/topic steps, each doomed to a
       synchronous ERROR against a session that's already dead. Confirmed on
       real hardware: without this check, a CONNLOST mid-sequence could add
       several seconds of pointless ERROR round-trips before doIdle() ever
       got a chance to notice and start reconnecting. */
    if (!mqtt.connected) { setState(ST_IDLE); return; }

    switch (step) {
    case 0: {
        /* AT+CMQTTSUB, like AT+CMQTTTOPIC/PAYLOAD, takes a length and then
           expects exactly that many raw topic bytes — not an inline quoted
           string (that form returned a synchronous ERROR on this modem). */
        int n = 0;
        const int topicMax = (int)sizeof(topicBuf) - 1;
        for (const char* p = mqtt.username; *p && n < topicMax; ) topicBuf[n++] = *p++;
        const char* mid = "/cmd/desired/#";
        for (const char* p = mid; *p && n < topicMax; ) topicBuf[n++] = *p++;
        topicBuf[n] = 0;

        int cn = 0;
        const char* pre = "AT+CMQTTSUB=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = ','; cmdBuf[cn++] = '1';  /* QoS 1 */
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) step++;
        break;
    }
    case 1:
        /* No manual ANS_MQTT_URC clear here — see the matching note in
           doMqttStart's CMQTTCONNECT step; same reasoning applies. */
        if (atCmd(topicBuf, 3000)) { t = core.getTick(); step++; }
        break;
    case 2:
        if (answer & ANS_MQTT_URC) step++;
        else if ((core.getTick() - t) >= 10000) { mqttScratch.urcResult = 0xFF; step++; }
        break;
    default:
        if (mqttScratch.urcResult == 0) { mqtt.connected = true; setState(ST_IDLE); }
        else {
            mqttScratch.teardownThenNet = false;
            setState(ST_MQTT_TEARDOWN);
        }
        break;
    }
}

void Modem::doMqttPub(void) {
    static uint8_t  pubIdx = 0;
    static uint8_t  scanFrom = 0;        /* round-robin cursor for case 0's scan below —
                                             resume just past the last-serviced slot instead
                                             of always rescanning from index 0, so a low-index
                                             field that's dirty on every pass (e.g. a fast-
                                             churning device-discovery entry) can't starve a
                                             rarer, higher-index one indefinitely. Confirmed on
                                             real hardware 2026-08-29: canRelayStep/canRelayProgress
                                             (registered late, high slot index) got starved during
                                             a CAN relay by lower-index traffic, hiding the web
                                             UI's progress bar even though the relay itself
                                             finished successfully. */
    static char     topicBuf[64];
    static char     cmdBuf[32];
    static uint32_t t = 0;
    static bool     pubFailed = false;   /* any step this attempt hit ANS_ERROR */
    static uint8_t  pubFailStreak = 0;   /* consecutive failed attempts */

    /* See the matching check/comment in doMqttSub() — same reasoning: don't
       grind through remaining CMQTTTOPIC/PAYLOAD/PUB steps against a session
       CMQTTCONNLOST already killed. The in-flight entry is left dirty (not
       cleared here), so it just gets retried once reconnected. */
    if (!mqtt.connected) { setState(ST_IDLE); return; }

    switch (step) {
    case 0: {
        /* atCmd() only actually (re-)transmits when its command STRING's hash
           changes — while step stays 0 waiting for a response, case 0 keeps
           re-running every tick, so picking pubIdx/building cmdBuf must
           happen exactly once per attempt, not on every re-entry. The old
           "always scan from index 0" version got away with rebuilding on
           every re-entry because it deterministically re-found the same
           entry each time (same string -> same hash -> atCmd() recognised
           it as still in flight). The round-robin scan below advances
           scanFrom as soon as an entry is picked, so a naive rebuild on
           every re-entry would pick a DIFFERENT entry each tick before the
           first one's response ever arrived — confirmed on real hardware
           2026-08-29: a storm of distinct AT+CMQTTTOPIC commands fired ~1ms
           apart, each abandoning the previous still-unanswered one. Gating
           the pick behind `picked` restores the one-attempt-per-command
           invariant regardless of scan order. */
        static bool picked = false;
        if (!picked) {
            bool found = false;
            for (uint8_t n = 0; n < MQTT_PUB_MAX; n++) {
                uint8_t i = (uint8_t)((scanFrom + n) % MQTT_PUB_MAX);
                if (mqttScratch.pubQueue[i].dirty) { pubIdx = i; found = true; break; }
            }
            if (!found) { setState(ST_IDLE); return; }
            scanFrom = (uint8_t)((pubIdx + 1) % MQTT_PUB_MAX);
            picked = true;

            pubFailed = false;

            int n = 0;
            const int topicMax = (int)sizeof(topicBuf) - 1;
            for (const char* p = mqtt.username; *p && n < topicMax; ) topicBuf[n++] = *p++;
            const char* mid = "/cmd/actual/";
            for (const char* p = mid; *p && n < topicMax; ) topicBuf[n++] = *p++;
            for (const char* p = mqttScratch.pubQueue[pubIdx].name; *p && n < topicMax; ) topicBuf[n++] = *p++;
            topicBuf[n] = 0;
        }

        int cn = 0;
        const char* pre = "AT+CMQTTTOPIC=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)strlen(topicBuf));
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) { if (answer & ANS_ERROR) pubFailed = true; step++; picked = false; }
        break;
    }
    case 1:
        /* Raw topic bytes, no line terminator — the modem is waiting for
           exactly the byte count declared above, not an AT command line. */
        if (atCmd(topicBuf, 3000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    case 2: {
        int n = (int)strlen(mqttScratch.pubQueue[pubIdx].value);
        int cn = 0;
        const char* pre = "AT+CMQTTPAYLOAD=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    }
    case 3:
        if (atCmd(mqttScratch.pubQueue[pubIdx].value, 3000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    case 4:
        /* <client_index>,<qos>,<pub_timeout>,<retained> — retained=1 so a late
           subscriber (or one that just reconnected) immediately gets the last
           known actual value instead of waiting for the next change. */
        if (atCmd("AT+CMQTTPUB=0,1,60,1\r\n", 3000)) {
            /* No manual ANS_MQTT_URC clear here — see the matching note in
               doMqttStart's CMQTTCONNECT step; same reasoning applies. */
            if (answer & ANS_ERROR) pubFailed = true;
            t = core.getTick(); step++;
        }
        break;
    case 5:
        if (answer & ANS_MQTT_URC) step++;
        else if ((core.getTick() - t) >= 15000) { pubFailed = true; step++; }
        break;
    default:
        /* Published (or gave up) — clear dirty regardless; a real change will
           re-mark it dirty and retry next pass rather than looping forever
           on one stuck entry. */
        mqttScratch.pubQueue[pubIdx].dirty = false;

        /* Track consecutive publish failures — 5 in a row means the MQTT
           session is actually dead even though we never got a clean
           disconnect notice (e.g. the transport blipped silently, seen on
           real hardware: mosquitto itself logged nothing, yet publishes
           errored for a while). One success proves it's alive again right
           away. This doesn't itself force a reconnect — it just corrects
           mqtt.connected to match reality — but doIdle()'s existing
           "!mqtt.connected && internetAllowed && internet.isInternetConnected, retry
           after 30s" check reacts to the same flag, so a real ST_MQTT_START
           retry does follow if the streak hits 5 and nothing else clears it
           first. That's the same recovery path any other disconnect already
           goes through, not new behavior introduced here. */
        if (pubFailed) {
            if (pubFailStreak < 5) pubFailStreak++;
            if (pubFailStreak >= 5) mqtt.connected = false;
        } else {
            pubFailStreak = 0;
            mqtt.connected = true;
        }
        setState(ST_IDLE);
        break;
    }
}

void Modem::doMqttTeardown(void) {
    static char topicBuf[64];
    static char cmdBuf[32];
    static uint32_t t = 0;

    /* Only announce "online"="0" if we actually had a live session to
       announce from (mqtt.connected still reflects that — only cleared in
       this function's own final step below). If we're here to reset a
       failed/stuck connect attempt instead, there's nothing to say
       goodbye from — skip straight to the DISC/REL/STOP reset, same as
       before this feature existed. The Last Will (see doMqttStart) covers
       the case this function *doesn't* run at all — a crash/power-loss
       with no chance to tear down gracefully. */
    if (step == 0 && !mqtt.connected) step = 3;

    switch (step) {
    case 0: {
        int n = 0;
        const int topicMax = (int)sizeof(topicBuf) - 1;
        for (const char* p = mqtt.username; *p && n < topicMax; ) topicBuf[n++] = *p++;
        const char* mid = "/cmd/actual/online";
        for (const char* p = mid; *p && n < topicMax; ) topicBuf[n++] = *p++;
        topicBuf[n] = 0;

        int cn = 0;
        const char* pre = "AT+CMQTTTOPIC=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) step++;
        break;
    }
    case 1:
        if (atCmd(topicBuf, 3000)) step++;
        break;
    case 2: {
        if (atCmd("AT+CMQTTPAYLOAD=0,1\r\n", 2000)) step++;
        break;
    }
    case 3:
        /* Reached directly (step forced to 3 above) when there was no live
           session to announce from — "0" is otherwise the 1-byte payload
           for the announcement started in case 0. */
        if (!mqtt.connected || atCmd("0", 3000)) step++;
        break;
    case 4:
        if (!mqtt.connected) { step++; break; }
        /* No manual ANS_MQTT_URC clear here — see the matching note in
           doMqttStart's CMQTTCONNECT step; same reasoning applies. */
        if (atCmd("AT+CMQTTPUB=0,1,60,1\r\n", 3000)) { t = core.getTick(); step++; }
        break;
    case 5:
        if (!mqtt.connected || (answer & ANS_MQTT_URC) || (core.getTick() - t) >= 15000) step++;
        break;
    case 6: if (atCmd("AT+CMQTTDISC=0,60\r\n", 5000)) step++; break;
    case 7: if (atCmd("AT+CMQTTREL=0\r\n",      3000)) step++; break;
    case 8: if (atCmd("AT+CMQTTSTOP\r\n",       3000)) step++; break;
    default:
        mqtt.connected = false;
        /* mqttScratch.teardownThenNet: true when called because config.useInternet was cleared
           or internet was lost (also tear down the PDP context); false when
           called to reset a stuck/failed MQTT session — internet stays up
           and timers.mqttRetry will bring MQTT back on its own. */
        if (mqttScratch.teardownThenNet) setState(ST_NET_TEARDOWN);
        else                     setState(ST_IDLE);
        break;
    }
}

/* ── OTA (MBC-2 firmware staging) ────────────────────────────────────────
   Downloads firmware/mbc2/<version>/{firmware.crc16,firmware.bin} from the
   same host as mqtt.broker, plain HTTP on port 3000 (bypassing nginx/TLS —
   this AT+HTTP stack is only proven against plain http:// so far, see
   internet.internetCheckUrl's own default; unlike the getlink URL, this is fetched
   by the modem itself, not opened in a browser, so there's no cert-name
   requirement pulling it toward https://). See host/README.md's "Firmware
   OTA" section for how a version gets published there, and flash.h for the
   128 KB / 64-page staging area this fills. Triggered by startOta(), called
   from onMqttCommandReceived() (Timberline.cpp) on cmd/desired/otaStart. */

void Modem::startOta(uint8_t deviceType, const char* version) {
    otaScratch.deviceType = deviceType;
    strncpy(otaScratch.version, version, sizeof(otaScratch.version) - 1);
    otaScratch.version[sizeof(otaScratch.version) - 1] = 0;
    otaScratch.startRequested = true;
    log_info("[OTA] start type=");
    char buf[4]; int n = appendUint(buf, 0, deviceType); buf[n] = 0;
    log_info(buf);
    log_info(" version=");
    log_info(otaScratch.version);
    log_info("\r\n");
}

/* Called from Timberline's CAN dispatch (ProcessCanMessage(), PGN 1) when
   the panel's "Auto-register" button is pressed. Only meaningful on a device
   that's never been configured (mqtt.username empty) — the caller already
   guards on that, this just flags the request; doIdle() picks it up once
   internet is confirmed up, same reentrancy rationale as startOta(). */
void Modem::startAutoRegister(void) {
    regScratch.startRequested = true;
    log_info("[AUTOREG] requested\r\n");
}

/* Auto-registration: the panel's "Auto-register" button, over CAN, asks the
   modem to invent its own MQTT login/password, register a new account with
   the backend over HTTPS, and finish exactly like "getlink" would (fresh
   token, retained publish, connectionLink built) — so a QR code just
   appears on the panel with no SMS round-trip at all. First time this
   firmware has ever done HTTPS (AT+HTTPPARA="URL","https://…") — every
   prior AT+HTTPACTION call (OTA, the example.com check) used plain http://.
   Per SIMCOM's HTTP AT command manual, HTTPS is simply the https:// scheme
   in the URL (default SSL context 0) — not yet verified on this exact
   module; if it doesn't work as-is, see the plan doc for the plain-HTTP
   fallback. */
void Modem::doAutoRegister(void) {
    static char cmd[192];
    static uint32_t t = 0;

    switch (step) {
    case 0:
        /* Belt-and-suspenders alongside the constructor/sanitizeString()
           defaults (Modem.cpp / flash.cpp): those only apply to genuinely
           untouched flash — sanitizeString() deliberately treats an already-
           persisted *empty* broker as an intentional value, not garbage, so
           a device that was ever flashed before this feature existed (and
           so already wrote an empty broker to flash the normal way) keeps
           it empty forever, no matter what the compiled-in default is.
           Confirmed on real hardware: exactly this happened, producing
           "https:///api/register-device..." — no host between the scheme
           and the path. Applying the fallback here too, right before it's
           actually used, closes that gap for every device regardless of
           flash history. */
        if (mqtt.broker[0] == 0) {
            strncpy(mqtt.broker, MODEM_DEFAULT_BROKER, sizeof(mqtt.broker) - 1);
            mqtt.broker[sizeof(mqtt.broker) - 1] = 0;
        }
        generateMemorableLogin(regScratch.login,    sizeof(regScratch.login));
        generateLinkToken(regScratch.password, sizeof(regScratch.password));
        step++;
        break;
    case 1: if (atCmd("AT+HTTPINIT\r\n", 2000)) step++; break;
    case 2: {
        int n = 0;
        const char* pre = "AT+HTTPPARA=\"URL\",\"https://";
        while (*pre) cmd[n++] = *pre++;
        for (const char* p = mqtt.broker; *p && n < 150; ) cmd[n++] = *p++;
        const char* mid = "/api/register-device?login=";
        while (*mid && n < 150) cmd[n++] = *mid++;
        for (const char* p = regScratch.login; *p && n < 170; ) cmd[n++] = *p++;
        const char* mid2 = "&password=";
        while (*mid2 && n < 175) cmd[n++] = *mid2++;
        for (const char* p = regScratch.password; *p && n < 185; ) cmd[n++] = *p++;
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 3:
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) { answer &= ~ANS_HTTPACTION; http.status = 0; step = 5; }
            else { answer &= ~ANS_HTTPACTION; t = core.getTick(); step++; }
        }
        break;
    case 4:
        if (answer & ANS_HTTPACTION) step = 5;
        else if ((core.getTick() - t) >= 20000) { http.status = 0; step = 5; }
        break;
    case 5: if (atCmd("AT+HTTPTERM\r\n", 2000)) step++; break;
    default:
        if (http.status == 200) {
            strncpy(mqtt.username, regScratch.login,    sizeof(mqtt.username) - 1);
            mqtt.username[sizeof(mqtt.username) - 1] = 0;
            strncpy(mqtt.password, regScratch.password, sizeof(mqtt.password) - 1);
            mqtt.password[sizeof(mqtt.password) - 1] = 0;
            /* flash write handled centrally by dataActualizator.handler(),
               same as TL_CMD_LOGIN/PASSWORD. */
            mqttForceReconnect();
            publishLinkToken();
            autoRegisterStatus = AUTOREG_DONE;
            log_info("[AUTOREG] done, login=");
            log_info(mqtt.username);
            log_info("\r\n");
            setState(ST_IDLE);
        } else if (http.status == 409 && regScratch.retries < 3) {
            /* Login collision — regenerate just the login (keep the same
               password) and retry the HTTP round-trip. Jump straight to
               step 1 (skip case 0), not setState(): still the same state,
               and setState() would reset step to 0 and regenerate the
               password too. */
            regScratch.retries++;
            generateMemorableLogin(regScratch.login, sizeof(regScratch.login));
            log_info("[AUTOREG] login taken, retrying\r\n");
            answer = 0; capture = CAP_NONE; step = 1;
        } else {
            autoRegisterStatus = AUTOREG_ERROR;
            log_info("[AUTOREG] failed, http.status=");
            char buf[4]; int n = appendUint(buf, 0, http.status); buf[n] = 0;
            log_info(buf);
            log_info("\r\n");
            setState(ST_IDLE);
        }
        break;
    }
}

/* Always-visible USB debug output for the OTA flow (log_error()/log_info()
   are unconditional — unlike log_at(), they aren't gated by the current
   log-level debug command, see log.c — so these show up on the console
   regardless of what "g"/"a"/"l" mode it's currently in). Kept to plain
   log_error()/log_info() (no printf-style formatting available here) by
   hand-building a short line via appendUint(). */
static void logOtaFail(uint8_t stepNum, const char* reason, uint32_t extra) {
    static char buf[96];
    int n = 0;
    const char* pre = "[OTA] FAIL step ";
    while (*pre) buf[n++] = *pre++;
    n = appendUint(buf, n, stepNum);
    buf[n++] = ' '; buf[n++] = '(';
    for (const char* p = reason; *p && n < 80; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = appendUint(buf, n, extra);
    buf[n++] = ')'; buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_error(buf);
}
static void logOtaInfo(const char* msg) {
    log_info("[OTA] ");
    log_info(msg);
    log_info("\r\n");
}
static void logOtaInfoNum(const char* label, uint32_t v) {
    static char buf[64];
    int n = 0;
    const char* pre = "[OTA] ";
    while (*pre) buf[n++] = *pre++;
    for (const char* p = label; *p && n < 50; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = appendUint(buf, n, v);
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_info(buf);
}

/* Builds "http://<mqttBroker>:3000/firmware/<deviceType>/<version>/<filename>"
   into out (must be >= 160 bytes) — deviceType picks the folder (see
   ProcessMessage()/OmniProtocol device-type table) instead of the old
   hardcoded "mbc2" segment, so any device type works as long as the
   matching folder exists on the server. */
static void buildOtaUrl(char* out, const char* mqttBroker, uint8_t deviceType, const char* version, const char* filename) {
    int n = 0;
    const char* pre = "http://";
    while (*pre) out[n++] = *pre++;
    for (const char* p = mqttBroker; *p && n < 150; ) out[n++] = *p++;
    const char* mid = ":3000/firmware/";
    while (*mid && n < 150) out[n++] = *mid++;
    n = appendUint(out, n, deviceType);
    if (n < 150) out[n++] = '/';
    for (const char* p = version; *p && n < 150; ) out[n++] = *p++;
    if (n < 150) out[n++] = '/';
    for (const char* p = filename; *p && n < 155; ) out[n++] = *p++;
    out[n] = 0;
}

/* Known bootloader versions and which relay algorithm is safe for each —
   the sole source of truth (2026-08-29, user's call: "никаких загрузок
   списков бутлодеров. Только то что есть - если нет в списке - значит
   пора обновлять сам модем" — no downloading bootloader lists at all; if a
   version isn't in this compiled-in table, that means the modem's own
   firmware needs updating, not a runtime fetch). Previously fetched from
   "/bootloaders.txt" at relay time (and, before that, alongside a Load) —
   both fetch paths removed the same day; update this array by hand and
   ship a new modem build when a new bootloader version needs adding. */
#define BINAR_TYPES 23,27,34,35,43,44,0,0
static const Modem::BootloaderEntry BUILTIN_BOOTLOADERS[] = {
    { {123,0,0,4},  1, {BINAR_TYPES} },
    { {123,0,0,11}, 2, {BINAR_TYPES} },
    { {123,0,0,13}, 3, {BINAR_TYPES} },
    { {123,0,2,4},  1, {125,0,0,0,0,0,0,0} },
    { {123,0,2,5},  1, {125,0,0,0,0,0,0,0} },
    { {123,0,2,6},  2, {125,0,0,0,0,0,0,0} },
    { {123,0,2,7},  2, {125,0,0,0,0,0,0,0} },
    { {123,0,2,13}, 3, {125,0,0,0,0,0,0,0} },
		{ {123,0,3,13}, 3, {126,0,0,0,0,0,0,0} },
};
#undef BINAR_TYPES
static const uint8_t BUILTIN_BOOTLOADERS_COUNT = sizeof(BUILTIN_BOOTLOADERS) / sizeof(BUILTIN_BOOTLOADERS[0]);

/* Matches a reported version against a BUILTIN_BOOTLOADERS entry ignoring
   byte[1] — that's the supply-voltage variant (0=12V, 1=24V per the CAN
   protocol doc), which doesn't affect the flashing protocol or which
   device types a build targets, and isn't used for anything else in this
   codebase yet (user's call, 2026-08-29: "вторая цифра... пока нигде не
   используется, но пусть пока её не будет проверять"). byte[0] (123,
   fixed) and byte[2] (the family selector — see BootloaderEntry's comment
   in Modem.h) are what actually distinguish entries; byte[3] (specific
   build) still has to match exactly, since different builds within the
   same family can carry different algorithm safety. Without ignoring
   byte[1], a real 24V device's bootloader would never match a table built
   from 12V samples at all, even though it's otherwise the identical
   build. */
static bool bootloaderVersionMatches(const uint8_t* tableVersion, const uint8_t* reportedVersion) {
    return tableVersion[0] == reportedVersion[0]
        && tableVersion[2] == reportedVersion[2]
        && tableVersion[3] == reportedVersion[3];
}

uint8_t Modem::lookupBootloaderAlgorithm(const uint8_t* version) {
    for (uint8_t i = 0; i < BUILTIN_BOOTLOADERS_COUNT; i++)
        if (bootloaderVersionMatches(BUILTIN_BOOTLOADERS[i].version, version)) return BUILTIN_BOOTLOADERS[i].algorithm;
    return 0;  /* not found — Timberline::doCanRelay() treats 0 the same as
                  a known-unsafe algorithm and refuses to relay; the fix is
                  a modem firmware update that adds this version above, not
                  a retry */
}

/* Refuses a relay whose target device type doesn't match what this
   specific bootloader build is actually meant for — see BootloaderEntry's
   own comment in Modem.h for the known families and why this check exists
   separately from the algorithm one above (a safe *protocol* doesn't mean
   the *image* is right for this hardware). Returns false for a version not
   in the table at all too — lookupBootloaderAlgorithm() already refuses
   that case (algorithm 0), this is just consistent about it rather than
   claiming "supported" by default. */
bool Modem::isDeviceTypeSupportedByBootloader(const uint8_t* version, uint8_t deviceType) {
    for (uint8_t i = 0; i < BUILTIN_BOOTLOADERS_COUNT; i++) {
        if (!bootloaderVersionMatches(BUILTIN_BOOTLOADERS[i].version, version)) continue;
        for (uint8_t j = 0; j < 8 && BUILTIN_BOOTLOADERS[i].supportedTypes[j] != 0; j++)
            if (BUILTIN_BOOTLOADERS[i].supportedTypes[j] == deviceType) return true;
        return false;
    }
    return false;
}

/* Fetches "/firmware/<otaScratch.deviceType>/<otaScratch.version>/profile"
   (built with buildOtaUrl() above — same "<type>/<version>/<filename>"
   shape as the firmware image itself, no separate URL builder needed) and
   parses it into ota.flashBase — the one piece of flash layout
   doCanRelay() needs now (see its case 2 comment for why there's no
   per-sector erase list any more, removed 2026-08-29). Per-*version* now,
   not per-type: the server derives this on the fly from the matched
   firmware file's own name (see host/README.md), so different versions of
   the same device type can carry different flash bases without a shared
   file going stale. Runs once, right before doOta() fetches the firmware
   image itself (see ST_FETCH_PROFILE in doIdle()) — same proven
   HTTPINIT/HTTPPARA/HTTPACTION/HTTPREAD(x2)/HTTPTERM sequence as doOta()'s
   own firmware.crc16 fetch below, just against a much smaller plain-text
   file instead of a binary CRC array. Expected format, one KEY=VALUE per
   line:
     flashBase=0x08020000
   A profile that's missing, unreachable, or has a malformed/missing
   flashBase= fails the whole otaStart the same way a missing
   firmware.crc16 already does — there's no safe default flash base to
   fall back to for an unknown device type/version (unlike the erase
   command, this one genuinely differs per firmware and can't be a single
   hardcoded constant). */
void Modem::doFetchProfile(void) {
    static char cmd[192];
    static char url[160];
    static char profileBuf[96];
    static uint16_t profileLen;
    static uint32_t t = 0;

    switch (step) {
    case 0: if (atCmd("AT+HTTPINIT\r\n", 2000)) step++; break;
    case 1: {
        buildOtaUrl(url, mqtt.broker, otaScratch.deviceType, otaScratch.version, "profile");
        int n = 0;
        const char* pre = "AT+HTTPPARA=\"URL\",\"";
        while (*pre) cmd[n++] = *pre++;
        for (const char* p = url; *p && n < 185; ) cmd[n++] = *p++;
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 2:
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) { logOtaFail(2, "profile-httpaction-cmd-error", 0); otaScratch.failed = true; step = 6; }
            else { answer &= ~ANS_HTTPACTION; t = core.getTick(); step++; }
        }
        break;
    case 3:
        if (answer & ANS_HTTPACTION) {
            if (http.status != 200 || http.dataLen == 0) {
                logOtaFail(3, "profile-http-status", http.status);
                otaScratch.failed = true; step = 6;
            }
            else step++;
        } else if ((core.getTick() - t) >= 20000) { logOtaFail(3, "profile-http-timeout", 0); otaScratch.failed = true; step = 6; }
        break;
    case 4: {
        uint16_t len = http.dataLen;
        if (len > sizeof(profileBuf) - 1) len = sizeof(profileBuf) - 1;
        profileLen = len;
        rawCapture.dst = (uint8_t*)profileBuf;
        rawCapture.cap = sizeof(profileBuf) - 1;
        rawCapture.got = 0;
        rawCapture.chunkRemaining = 0;
        int n = 0;
        const char* pre = "AT+HTTPREAD=0,";
        while (*pre) cmd[n++] = *pre++;
        n = appendUint(cmd, n, len);
        cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 3000)) {
            if (answer & ANS_ERROR) { rawCapture.dst = 0; logOtaFail(4, "profile-httpread-cmd-error", 0); otaScratch.failed = true; step = 6; break; }
            answer &= ~ANS_HTTPREAD_DONE;
            t = core.getTick();
            step++;
        }
        break;
    }
    case 5: {
        if (answer & ANS_HTTPREAD_DONE) {
            rawCapture.dst = 0;
            if (rawCapture.got != profileLen) { logOtaFail(5, "profile-len-mismatch", rawCapture.got); otaScratch.failed = true; step = 6; break; }
            profileBuf[profileLen] = 0;

            /* Hand-rolled KEY=VALUE scan — no sscanf/JSON parser anywhere in
               this firmware, same style as the rest of the AT-response
               parsing in this file. */
            bool gotBase = false;
            const char* baseKey = strstr(profileBuf, "flashBase=");
            if (baseKey) {
                const char* p = baseKey + 10;  /* strlen("flashBase=") */
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
                uint32_t v = 0; bool any = false;
                for (; *p; p++) {
                    uint8_t d;
                    if (*p >= '0' && *p <= '9') d = (uint8_t)(*p - '0');
                    else if (*p >= 'a' && *p <= 'f') d = (uint8_t)(*p - 'a' + 10);
                    else if (*p >= 'A' && *p <= 'F') d = (uint8_t)(*p - 'A' + 10);
                    else break;
                    v = (v << 4) | d; any = true;
                }
                if (any) { ota.flashBase = v; gotBase = true; }
            }
            if (!gotBase) { logOtaFail(5, "profile-parse-failed", 0); otaScratch.failed = true; }
            else { ota.deviceType = otaScratch.deviceType; logOtaInfoNum("profile flashBase", ota.flashBase); }
            step = 6;
        } else if ((core.getTick() - t) >= 8000) {
            rawCapture.dst = 0;
            logOtaFail(5, "profile-httpread-timeout", rawCapture.got);
            otaScratch.failed = true;
            step = 6;
        }
        break;
    }
    default:
        if (atCmd("AT+HTTPTERM\r\n", 2000)) {
            if (otaScratch.failed) { ota.status = OTA_ERROR; setState(ST_IDLE); }
            else setState(ST_OTA);
        }
        break;
    }
}

void Modem::doOta(void) {
    static char cmd[192];
    static char url[160];
    static uint32_t t = 0;

    /* deviceType == VERSION_1: this modem's own firmware, staged into the
       separate self-OTA buffer (FLASH_SELF_OTA_BUF_ADDR, see flash.h)
       instead of the shared target-device one — same download/verify loop
       below either way, just pointed at a different (smaller) flash region
       via these two locals. */
    const bool     self     = (otaScratch.deviceType == VERSION_1);
    const uint16_t maxPages = self ? MODEM_SELF_OTA_PAGE_COUNT : MODEM_OTA_PAGE_COUNT;

    /* Steps: 0-5 fetch+parse firmware.crc16 (one small HTTPACTION+HTTPREAD).
       6 scans all pages already in flash against it — if everything already
       matches (resuming a finished run, or restarting an unchanged version),
       skips straight to 20 without touching the network at all. Otherwise
       7-9 do one HTTPACTION for firmware.bin — the *whole* file is fetched
       into the module's own HTTP response buffer there; every page in the
       10+ loop below reads a slice of that same cached response via
       HTTPREAD's offset, with no further network fetches (standard AT+HTTP
       semantics — HTTPACTION fetches once, HTTPREAD pages through the
       result). This assumes the module can buffer the full ~128 KB response
       internally; not yet confirmed against real A7682E hardware — if it
       can't, this needs reworking into a per-page HTTPACTION using
       server-side HTTP Range instead. 20 is cleanup (always runs, success or
       failure — releases the HTTP session via HTTPTERM either way).

       Every AT+HTTPREAD (steps 4-5 and 11-12) is two steps, not one — see
       the long comment on RawCapture in Modem.h: confirmed on real hardware
       that its "OK" only means the command was *accepted*, not that the
       data arrived. The actual payload shows up afterward, asynchronously,
       as one or more "+HTTPREAD: <len>" chunks (module caps each around
       1024 bytes) ending in a "+HTTPREAD: 0" terminator. Treating that
       first OK as "done" (the original bug here) let the state machine
       move on before the real data existed — the bytes then arrived later
       with nothing armed to catch them, corrupting the line parser (visible
       on a USB debug terminal as garbage/ERRORs) and leaving stale/garbage
       CRC data behind. The "send" step only confirms the command was
       accepted; a separate "wait" step polls for ANS_HTTPREAD_DONE. */
    switch (step) {
    case 0: if (atCmd("AT+HTTPINIT\r\n", 2000)) step++; break;
    case 1: {
        buildOtaUrl(url, mqtt.broker, otaScratch.deviceType, otaScratch.version, "firmware.crc16");
        int n = 0;
        const char* pre = "AT+HTTPPARA=\"URL\",\"";
        while (*pre) cmd[n++] = *pre++;
        for (const char* p = url; *p && n < 185; ) cmd[n++] = *p++;
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 2:
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) { logOtaFail(2, "crc-httpaction-cmd-error", 0); otaScratch.failed = true; step = 20; }
            else { answer &= ~ANS_HTTPACTION; t = core.getTick(); step++; }
        }
        break;
    case 3:
        if (answer & ANS_HTTPACTION) {
            if (http.status != 200 || http.dataLen == 0) {
                logOtaFail(3, "crc-http-status", http.status);
                otaScratch.failed = true; step = 20;
            }
            else step++;
        } else if ((core.getTick() - t) >= 20000) { logOtaFail(3, "crc-http-timeout", 0); otaScratch.failed = true; step = 20; }
        break;
    case 4: {
        /* Clamp to otaScratch.pageCrc's capacity (MODEM_OTA_PAGE_COUNT*2 bytes) —
           a firmware.crc16 bigger than that would mean an image bigger than
           the staging area can hold, which is a server-side mistake, not
           something to crash over here. */
        uint16_t len = http.dataLen;
        if (len > sizeof(otaScratch.pageCrc)) len = sizeof(otaScratch.pageCrc);
        otaScratch.readLen = len;
        rawCapture.dst = (uint8_t*)otaScratch.pageCrc;
        rawCapture.cap = sizeof(otaScratch.pageCrc);
        rawCapture.got = 0;
        rawCapture.chunkRemaining = 0;  /* armed but not yet started — see drainRx() */
        int n = 0;
        const char* pre = "AT+HTTPREAD=0,";
        while (*pre) cmd[n++] = *pre++;
        n = appendUint(cmd, n, len);
        cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 3000)) {
            if (answer & ANS_ERROR) { rawCapture.dst = 0; logOtaFail(4, "crc-httpread-cmd-error", 0); otaScratch.failed = true; step = 20; break; }
            answer &= ~ANS_HTTPREAD_DONE;
            t = core.getTick();
            step++;
        }
        break;
    }
    case 5: {
        uint16_t len = otaScratch.readLen; /* stashed by case 4 — http.dataLen itself gets
                                               overwritten by every "+HTTPREAD: <n>" chunk header,
                                               including the final zero-length terminator, so it
                                               can't be trusted here. */
        if (answer & ANS_HTTPREAD_DONE) {
            rawCapture.dst = 0;
            if (rawCapture.got != len) { logOtaFail(5, "crc-len-mismatch", rawCapture.got); otaScratch.failed = true; step = 20; break; }
            ota.pageTotal = (uint16_t)(rawCapture.got / 2);
            if (ota.pageTotal > maxPages) ota.pageTotal = maxPages;
            ota.page = 0;
            logOtaInfoNum("crc-ok-pages", ota.pageTotal);
            step++;
        } else if ((core.getTick() - t) >= 8000) {
            rawCapture.dst = 0;
            logOtaFail(5, "crc-httpread-timeout", rawCapture.got);
            otaScratch.failed = true; step = 20;
        }
        break;
    }
    case 6: {
        /* Cheap up-front scan (flash reads + CRC16 only, no network) — if
           every page this modem already has staged matches the target
           version, there is nothing to download at all. Re-running otaStart
           for an already-fully-staged version (or one identical to what's
           already there) becomes a near no-op instead of a full re-fetch. */
        bool allMatch = true;
        for (uint16_t p = 0; p < ota.pageTotal; p++) {
            uint16_t pageCrc = self ? flash.crc16SelfOtaPage(p) : flash.crc16OtaPage(p);
            if (pageCrc != otaScratch.pageCrc[p]) { allMatch = false; break; }
        }
        if (allMatch) { ota.page = ota.pageTotal; step = 20; }
        else { buildOtaUrl(url, mqtt.broker, otaScratch.deviceType, otaScratch.version, "firmware.bin"); step = 10; }
        break;
    }

    /* ── per-page loop ────────────────────────────────────────────────────
       Confirmed on real hardware: a single AT+HTTPACTION fetching the whole
       ~128 KB firmware.bin into the module's own cache, then paging through
       it via many AT+HTTPREAD offsets, works for the first 63 of 64 pages
       every time — but the very last page comes back with the right byte
       count and the wrong content, deterministically, even after three full
       fresh re-fetches of the entire file. That rules out a transient radio
       glitch; it points at a hard internal cache/buffer limit near the tail
       of a response that size. AT+HTTPPARA="USERDATA" (the usual SIMCOM way
       to inject a Range header) was also tried to fetch just the missing
       tail — also confirmed ERROR on this module, so custom headers aren't
       an option either way. Server-side slicing instead: each page is its
       own small GET against a URL carrying ?offset=X&len=Y query params —
       server.js's dedicated /firmware/.../firmware.bin route (registered
       ahead of express.static) reads and returns just those bytes as a
       plain 200 response, so the module only ever has to cache 2 KB at a
       time and never comes near whatever its real limit is. */
    case 10:
        if (ota.page >= ota.pageTotal) { step = 20; break; }
        if ((self ? flash.crc16SelfOtaPage(ota.page) : flash.crc16OtaPage(ota.page)) == otaScratch.pageCrc[ota.page]) {
            /* Already correct in flash — from a previous interrupted run,
               or unchanged from whatever version was staged before. Skip
               straight to the next page without touching the network. */
            ota.page++;
            break;  /* re-enter case 10 next handler() call */
        }
        otaScratch.retries = 0;
        step = 11;
        break;
    case 11: {
        int n = 0;
        const char* pre = "AT+HTTPPARA=\"URL\",\"";
        while (*pre) cmd[n++] = *pre++;
        for (const char* p = url; *p && n < 150; ) cmd[n++] = *p++;
        const char* q = "?offset=";
        while (*q) cmd[n++] = *q++;
        n = appendUint(cmd, n, (uint32_t)ota.page * MODEM_OTA_PAGE_SIZE);
        const char* l = "&len=";
        while (*l) cmd[n++] = *l++;
        n = appendUint(cmd, n, MODEM_OTA_PAGE_SIZE);
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 12:
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) {
                if (++otaScratch.retries >= 5) { logOtaFail(12, "page-httpaction-cmd-retries", ota.page); otaScratch.failed = true; step = 20; }
                else step = 25;
                break;
            }
            answer &= ~ANS_HTTPACTION;
            t = core.getTick();
            step++;
        }
        break;
    case 13:
        if (answer & ANS_HTTPACTION) {
            if (http.status != 200) {
                if (++otaScratch.retries >= 5) { logOtaFail(13, "page-http-status-retries", http.status); otaScratch.failed = true; step = 20; }
                else step = 25;
                break;
            }
            step++;
        } else if ((core.getTick() - t) >= 15000) {
            if (++otaScratch.retries >= 5) { logOtaFail(13, "page-httpaction-timeout-retries", ota.page); otaScratch.failed = true; step = 20; }
            else step = 25;
        }
        break;
    case 14: {
        rawCapture.dst = otaScratch.chunkBuf;
        rawCapture.cap = sizeof(otaScratch.chunkBuf);
        rawCapture.got = 0;
        rawCapture.chunkRemaining = 0;
        int n = 0;
        const char* pre = "AT+HTTPREAD=0,";
        while (*pre) cmd[n++] = *pre++;
        n = appendUint(cmd, n, MODEM_OTA_PAGE_SIZE);
        cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 3000)) {
            if (answer & ANS_ERROR) {
                rawCapture.dst = 0;
                if (++otaScratch.retries >= 5) { logOtaFail(14, "page-httpread-cmd-retries", ota.page); otaScratch.failed = true; step = 20; }
                else step = 25;
                break;
            }
            answer &= ~ANS_HTTPREAD_DONE;
            t = core.getTick();
            step++;
        }
        break;
    }
    case 15:
        if (answer & ANS_HTTPREAD_DONE) {
            rawCapture.dst = 0;
            if (rawCapture.got != MODEM_OTA_PAGE_SIZE) {
                if (++otaScratch.retries >= 5) { logOtaFail(15, "page-len-mismatch-retries", ota.page); otaScratch.failed = true; step = 20; }
                else step = 25;
                break;
            }
            step++;
        } else if ((core.getTick() - t) >= 8000) {
            rawCapture.dst = 0;
            if (++otaScratch.retries >= 5) { logOtaFail(15, "page-httpread-timeout-retries", ota.page); otaScratch.failed = true; step = 20; }
            else step = 25;
        }
        break;
    case 16:
        if (flashCrc16(otaScratch.chunkBuf, MODEM_OTA_PAGE_SIZE) != otaScratch.pageCrc[ota.page]) {
            if (++otaScratch.retries >= 5) { logOtaFail(16, "page-crc-mismatch-retries", ota.page); otaScratch.failed = true; step = 20; break; }
            step = 25;  /* re-fetch this page from scratch, via the spacer */
            break;
        }
        step++;
        break;
    case 17:
        if (!(self ? flash.writeSelfOtaPage(ota.page, otaScratch.chunkBuf) : flash.writeOtaPage(ota.page, otaScratch.chunkBuf))) { logOtaFail(17, "flash-write-failed", ota.page); otaScratch.failed = true; step = 20; break; }
        step++;
        break;
    case 18:
        if (self) flash.readSelfOtaPage(ota.page, otaScratch.verifyBuf); else flash.readOtaPage(ota.page, otaScratch.verifyBuf);
        if (memcmp(otaScratch.chunkBuf, otaScratch.verifyBuf, MODEM_OTA_PAGE_SIZE) != 0) {
            /* Flash write didn't take — retry the whole page (re-download
               included, in case otaScratch.chunkBuf itself was somehow the problem,
               not just the flash write) rather than just rewriting the same
               possibly-bad RAM copy. */
            if (++otaScratch.retries >= 5) { logOtaFail(18, "flash-verify-mismatch-retries", ota.page); otaScratch.failed = true; step = 20; break; }
            step = 25;
            break;
        }
        ota.page++;
        step = 10;
        break;
    case 25:
        /* Spacer before re-issuing this page's AT+HTTPPARA/HTTPACTION/
           HTTPREAD sequence — atCmd() dedupes by comparing the command
           string's hash against the last one it sent (see the identical
           rationale in doSendSms() above); re-sending the exact same
           command right after a failure would just replay the stale answer
           bits instead of actually retransmitting. Any genuinely different
           command forces a real resend once we're back at case 11. */
        if (atCmd("AT+CSQ\r\n", 2000)) step = 11;
        break;

    case 20:
        if (atCmd("AT+HTTPTERM\r\n", 2000)) {
            if (!otaScratch.failed) {
                /* Every page was individually CRC16-verified against the
                   server's firmware.crc16 on its way into flash, but that
                   verification only ever lived in RAM (otaScratch.pageCrc)
                   — a reset right after "DONE" would leave the staged image
                   otherwise-unremarkable flash bytes with nothing recording
                   which version they are or whether they're still intact.
                   Persist it now, once, while everything's confirmed good:
                   see FLASH_OTA_META_ADDR in flash.h for why this is its
                   own sector instead of living in the regular settings
                   blob. */
                uint32_t totalBytes = (uint32_t)ota.pageTotal * MODEM_OTA_PAGE_SIZE;
                uint16_t totalCrc16 = flashCrc16((const uint8_t*)(self ? FLASH_SELF_OTA_BUF_ADDR : FLASH_OTA_BUF_ADDR), totalBytes);
                if (self) flash.writeSelfOtaMeta(otaScratch.version, totalBytes, totalCrc16);
                else      flash.writeOtaMeta(otaScratch.version, totalBytes, totalCrc16, ota.flashBase);
                refreshStagedInfo();  /* re-read rather than trust the write blindly succeeded */
            }
            ota.status = otaScratch.failed ? OTA_ERROR : OTA_DONE;
            logOtaInfo(otaScratch.failed ? "result=ERROR" : "result=DONE");
            setState(ST_IDLE);
        }
        break;
    }
}
