#include "Modem.h"
#include "modem_handler.h"
#include "core.h"
#include "log.h"
#include "operator_names.h"
#include <string.h>
#include <stdlib.h>

Modem   modem;


extern "C" void USART1_IRQHandler(void) {
    if (USART_GetIntStatus(USART1, USART_INT_RXDNE) != RESET)
        modem.usart.receiveIntHandler((uint8_t)USART_ReceiveData(USART1));
    if (USART_GetIntStatus(USART1, USART_INT_TXDE) != RESET)
        modem.txIsr();
}

/* ── Constructor ─────────────────────────────────────────────────────── */
Modem::Modem()
    : isRegistered(false), isRoaming(false), csq(0xFF),
      lac(0xFFFF), cellId(0xFFFFFFFF), networkAcT(0xFF),
      isInternetConnected(false),
      mqttConnected(false),
      telemetryIntervalSec(15),
      onMqttCommand(0),
      useInternet(false),
      tempUnit(0),
      allowRoaming(false),
      faultReport(true),
      cmdAck(true),
      onSmsReceived(0),
      smsDebugMode(false),
      answer(0), capture(CAP_NONE),
      state(ST_POWER_ON), step(0),
      smsPending(false), smsSlot(0), smsNotifySlot(0), smsNotifyPending(false),
      ussdPending(false),
      timerCsq(0), timerCreg(0), timerNet(0), timerMqttRetry(0), timerSmsPoll(0), httpStatus(0),
      mqttUrcResult(0), mqttTeardownThenNet(false), netTeardownThenReinit(false),
      mqttReconnectRequested(false),
      rxCursor(0), lineLen(0)
{
    imei[0] = iccid[0] = ownNumber[0] = operatorCode[0] = operatorName[0] = 0;
    smsPhone[0] = smsText[0] = cmgrPhone[0] = cmgrBody[0] = ussdReq[0] = 0;
    ipAddress[0] = 0;
    strncpy(internetCheckUrl, "http://google.com", sizeof(internetCheckUrl) - 1);
    internetCheckUrl[sizeof(internetCheckUrl) - 1] = 0;
    /* No baked-in default here on purpose — a real broker/account's
       credentials don't belong compiled into firmware that could end up
       on any device; must be set explicitly (SMS/CAN) before MQTT is
       usable. */
    mqttBroker[0] = 0;
    mqttUsername[0] = 0;
    mqttPassword[0] = 0;
    apn[0] = 0;
    apnUsername[0] = 0;
    apnPassword[0] = 0;
    mqttRxName[0] = 0; mqttRxPayload[0] = 0;
    for (int i = 0; i < MQTT_PUB_MAX; i++) {
        mqttPubQueue[i].name[0] = 0;
        mqttPubQueue[i].value[0] = 0;
        mqttPubQueue[i].dirty = false;
    }
    for (int i = 0; i < SMS_QUEUE_MAX; i++) {
        smsQueue[i].phone[0] = 0;
        smsQueue[i].text[0] = 0;
        smsQueue[i].pending = false;
    }
    for (int i = 0; i < 5; i++) phones[i][0] = 0;
    pin[0]='1'; pin[1]='2'; pin[2]='3'; pin[3]='4'; pin[4]='\0';
		
}

void Modem::txIsr(void) { usart.transmitNextByte(); }

/* ── initialize ──────────────────────────────────────────────────────── */
void Modem::initialize(void) {
		useInternet = true;
    usart.initialize(1, 115200);
    PowergoodPin.Initialize(GPIOA, GPIO_PIN_3, GPIO_Mode_IPU);
    DTRPin.Initialize(GPIOB, GPIO_PIN_1, GPIO_Mode_Out_PP);
    DTRPin.Reset();
    PowerkeyPin.Initialize(GPIOB, GPIO_PIN_7, GPIO_Mode_Out_PP);
    PowerkeyPin.Reset();
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
    }
}

/* ── sendSms ─────────────────────────────────────────────────────────── */
void Modem::sendSms(const char* phone, const char* text) {
    if (!phone || phone[0] != '+') return;

    log_info("[SMS] to: "); log_info(phone);
    log_info(" | ");        log_info(text);
    log_info("\r\n");

    if (smsDebugMode) return;

    if (!smsPending) {
        strncpy(smsPhone, phone, sizeof(smsPhone) - 1); smsPhone[sizeof(smsPhone)-1] = 0;
        strncpy(smsText,  text,  sizeof(smsText)  - 1); smsText[sizeof(smsText) -1] = 0;
        smsPending = true;
        return;
    }

    /* Active slot busy — queue it instead of silently dropping (see
       SmsQueueEntry comment in Modem.h). Never calls setState(): sendSms()
       can be invoked reentrantly from inside onSmsReceived (itself called
       from doReadSms()), same constraint as mqttForceReconnect(). */
    for (int i = 0; i < SMS_QUEUE_MAX; i++) {
        if (!smsQueue[i].pending) {
            strncpy(smsQueue[i].phone, phone, sizeof(smsQueue[i].phone) - 1);
            smsQueue[i].phone[sizeof(smsQueue[i].phone) - 1] = 0;
            strncpy(smsQueue[i].text, text, sizeof(smsQueue[i].text) - 1);
            smsQueue[i].text[sizeof(smsQueue[i].text) - 1] = 0;
            smsQueue[i].pending = true;
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

    while (rxCursor != usart.getBufferPos()) {
        char c = (char)usart.getByte(rxCursor++);
        if (rxCursor >= Usart_C::BUFFER_SIZE) rxCursor = 0;

        char dbg[2] = {c, 0};
        if (bridgeMode) {
            log_info(dbg);
        } else {
            if (atLineStart) { logAtTimestamp(); atLineStart = false; }
            log_at(dbg);
            if (c == '\n') atLineStart = true;
        }

        /* SMS prompt arrives as "> " without newline */
        if (c == '>' && lineLen == 0) { answer |= ANS_PROMPT; continue; }

        if (c == '\n') {
            lineBuf[lineLen] = 0;
            parseLine();
            lineLen = 0;
        } else if (c != '\r' && lineLen < LINE_SIZE - 1) {
            lineBuf[lineLen++] = c;
        }
    }
}

/* ── parseLine ───────────────────────────────────────────────────────── */

static bool starts(const char* s, const char* pre) {
    while (*pre) if (*s++ != *pre++) return false;
    return true;
}

/* Append the decimal digits of v to buf starting at n, return new n. */
static int appendUint(char* buf, int n, uint16_t v) {
    char tmp[6]; int t = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0 && t < 6) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
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

void Modem::parseLine(void) {
    const char* s = lineBuf;

    /* Multi-line capture takes priority */
    if (capture == CAP_IMEI) {
        /* IMEI is plain digits (15 of them). An unsolicited notification
           (e.g. "+CGEV: EPS PDN ACT ...") can land in this same window —
           don't mistake it for the IMEI; keep waiting for the real line. */
        bool looksLikeImei = (lineLen >= 14);
        for (uint16_t i = 0; looksLikeImei && i < lineLen; i++)
            if (s[i] < '0' || s[i] > '9') looksLikeImei = false;

        if (looksLikeImei) {
            strncpy(imei, s, sizeof(imei)-1); imei[sizeof(imei)-1] = 0;
            capture = CAP_NONE;
        }
        return;
    }
    if (capture == CAP_CMGR_BODY) {
        capture = CAP_NONE;
        strncpy(cmgrBody, s, sizeof(cmgrBody)-1); cmgrBody[sizeof(cmgrBody)-1] = 0;
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
        strncpy(mqttRxName, rest, sizeof(mqttRxName)-1); mqttRxName[sizeof(mqttRxName)-1] = 0;
        /* Defensively trim trailing whitespace — some MQTT clients/UIs are
           prone to a stray trailing space in a hand-typed topic. */
        for (int i = (int)strlen(mqttRxName) - 1; i >= 0 && mqttRxName[i] == ' '; i--) mqttRxName[i] = 0;
        return;
    }
    if (capture == CAP_MQTT_PAYLOAD) {
        capture = CAP_NONE;
        strncpy(mqttRxPayload, s, sizeof(mqttRxPayload)-1); mqttRxPayload[sizeof(mqttRxPayload)-1] = 0;
        for (int i = (int)strlen(mqttRxPayload) - 1; i >= 0 && mqttRxPayload[i] == ' '; i--) mqttRxPayload[i] = 0;
        if (onMqttCommand) onMqttCommand(mqttRxName, mqttRxPayload);
        return;
    }

    if (lineLen == 0) return;

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
        csq = (v >= 0 && v <= 31) ? (uint8_t)v : 0xFF;
        answer |= ANS_CSQ;
    }
    else if (starts(s,"+CREG: ")) {
        /* AT+CREG=2 makes both the poll reply and the URC carry lac/ci:
             poll: <n>,<stat>[,"<lac>","<ci>"]   URC: <stat>[,"<lac>","<ci>"]
           Collect the unquoted leading comma-separated fields — the last
           one is always <stat> regardless of whether <n> is present. */
        const char* p = s + 7;
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
        isRegistered = (stat == '1');
        isRoaming    = (stat == '5');

        if (*p == '"') {
            char hexLac[8], hexCi[12];
            nthQuoted(s + 7, 0, hexLac, sizeof(hexLac));
            nthQuoted(s + 7, 1, hexCi,  sizeof(hexCi));
            lac    = (uint16_t)strtol(hexLac, NULL, 16);
            cellId = (uint32_t)strtol(hexCi,  NULL, 16);
        }
        answer |= ANS_CREG;
    }
    else if (starts(s,"+COPS: ")) {
        /* AT+COPS=3,2 selects numeric format, so the 3rd field (oper) is a
           quoted MCC+MNC string, e.g. +COPS: 0,2,"25099",7 */
        char oper[8];
        nthQuoted(s + 7, 0, oper, sizeof(oper));
        if (oper[0]) {
            strncpy(operatorCode, oper, sizeof(operatorCode)-1); operatorCode[sizeof(operatorCode)-1] = 0;
            const char* name = findOperatorName(operatorCode);
            if (name) { strncpy(operatorName, name, sizeof(operatorName)-1); operatorName[sizeof(operatorName)-1] = 0; }
            else operatorName[0] = 0;
        }

        /* <AcT> is the optional 4th field, right after oper's closing quote:
           +COPS: <mode>,<format>,"<oper>"[,<AcT>] — plain (unquoted) integer,
           real network tech (0=GSM,2=UTRAN,7=E-UTRAN,...), sent to the panel
           as-is; it buckets it into 2G/3G/4G for display. */
        {
            const char* p = s + 7;
            int quotes = 0;
            while (*p && quotes < 2) { if (*p == '"') quotes++; p++; }
            networkAcT = (*p == ',') ? (uint8_t)strtol(p + 1, NULL, 10) : 0xFF;
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
        while (*p && *p != ',' && *p != '"' && i < sizeof(ipAddress)-1) ipAddress[i++] = *p++;
        ipAddress[i] = 0;
        answer |= ANS_CGPADDR;
    }
    else if (starts(s,"+HTTPACTION: ")) {
        /* +HTTPACTION: <method>,<status>,<datalen> — status is 2nd field */
        const char* p = s + 13;
        while (*p && *p != ',') p++;
        httpStatus = (*p == ',') ? (uint16_t)atoi(p + 1) : 0;
        answer |= ANS_HTTPACTION;
    }
    else if (starts(s,"+CMQTTCONNECT: ") || starts(s,"+CMQTTSUB: ") || starts(s,"+CMQTTPUB: ")) {
        /* All three share "<client_index>,<result>" — 0 = success */
        const char* p = s;
        while (*p && *p != ',') p++;
        mqttUrcResult = (*p == ',') ? (uint8_t)atoi(p + 1) : 0xFF;
        answer |= ANS_MQTT_URC;
    }
    else if (starts(s,"+CMQTTCONNLOST: ")) {
        mqttConnected = false;
    }
    else if (starts(s,"+CMQTTRXTOPIC: ")) {
        capture = CAP_MQTT_TOPIC;
    }
    else if (starts(s,"+CMQTTRXPAYLOAD: ")) {
        capture = CAP_MQTT_PAYLOAD;
    }
    else if (starts(s,"+CMTI: ")) {
        /* +CMTI: "SM",3 — only touches smsNotifySlot, never the live
           smsSlot doReadSms() might currently be mid-read on (see the field
           comments in Modem.h for why that distinction matters). */
        const char* p = s + 7;
        while (*p && *p != ',') p++;
        smsNotifySlot = (*p == ',') ? (uint8_t)(*(p+1) - '0') : 1;
        if (smsNotifySlot == 0) smsNotifySlot = 1;
        answer |= ANS_CMTI;
        smsNotifyPending = true;
    }
    else if (starts(s,"+CMGL: ")) {
        /* +CMGL: <idx>,"REC UNREAD",... — periodic fallback poll only (see
           ST_POLL_SMS_UNREAD); just need an index here, not the body/phone —
           the existing CMGR-based doReadSms() fetches those properly once
           doIdle() copies smsNotifySlot into smsSlot. If more than one
           unread message is sitting in storage, whichever index lands last
           just means the others get caught on a later poll after this one
           is read+deleted. */
        const char* p = s + 7;
        uint8_t idx = 0;
        while (*p >= '0' && *p <= '9') { idx = (uint8_t)(idx * 10 + (*p - '0')); p++; }
        if (idx > 0) { smsNotifySlot = idx; answer |= ANS_CMGL; }
    }
    else if (starts(s,"+CMGR:")) {
        /* +CMGR: "REC UNREAD","+79001234567",, ... — phone is 2nd quoted field */
        nthQuoted(s + 7, 1, cmgrPhone, sizeof(cmgrPhone));
        capture = CAP_CMGR_BODY;
        answer |= ANS_CMGR;
    }
    else if (starts(s,"+CMGS: ")) {
        answer |= ANS_CMGS;
    }
    else if (starts(s,"+ICCID: ")) {
        strncpy(iccid, s + 8, sizeof(iccid)-1); iccid[sizeof(iccid)-1] = 0;
        answer |= ANS_ICCID;
    }
    else if (starts(s,"+CNUM:")) {
        /* +CNUM: "","<number>",145 — own number is 2nd quoted field */
        nthQuoted(s + 7, 1, ownNumber, sizeof(ownNumber));
        answer |= ANS_CNUM;
    }
    else if (starts(s,"+CUSD:")) {
        char raw[128];
        nthQuoted(s + 7, 0, raw, sizeof(raw));

        /* Detect UCS2 hex: all chars are 0-9/A-F and length divisible by 4 */
        int rlen = (int)strlen(raw);
        bool isUcs2 = (rlen >= 4) && (rlen % 4 == 0);
        for (int i = 0; i < rlen && isUcs2; i++) {
            char c = raw[i];
            if (!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f')))
                isUcs2 = false;
        }

        static char decoded[128];
        if (isUcs2) {
            /* UTF-16BE hex → UTF-8 */
            int di = 0;
            for (int i = 0; i < rlen - 3 && di < 125; i += 4) {
                #define HV(c) ((uint8_t)((c)>='a'?(c)-'a'+10:(c)>='A'?(c)-'A'+10:(c)-'0'))
                uint16_t cp = (uint16_t)( ((uint16_t)HV(raw[i  ])<<12)
                                        | ((uint16_t)HV(raw[i+1])<< 8)
                                        | ((uint16_t)HV(raw[i+2])<< 4)
                                        |  (uint16_t)HV(raw[i+3]));
                #undef HV
                if (cp < 0x80) {
                    decoded[di++] = (char)cp;
                } else if (cp < 0x800) {
                    decoded[di++] = (char)(0xC0 | (cp >> 6));
                    decoded[di++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    decoded[di++] = (char)(0xE0 | ( cp >> 12));
                    decoded[di++] = (char)(0x80 | ((cp >>  6) & 0x3F));
                    decoded[di++] = (char)(0x80 | ( cp        & 0x3F));
                }
            }
            decoded[di] = 0;
        } else {
            strncpy(decoded, raw, sizeof(decoded) - 1);
            decoded[sizeof(decoded)-1] = 0;
        }

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
    case 12: if (atCmd("AT+COPS=3,2\r\n",          300)) step++; break;
    case 13: if (atCmd("AT+COPS?\r\n",            3000)) step++; break;
    default:
        log_info("Modem ready. IMEI="); log_info(imei[0]       ? imei       : "?");
        log_info(" SIM=");              log_info(ownNumber[0]   ? ownNumber  : "?");
        log_info("\r\n");
        timerCsq = timerCreg = core.getTick();
        setState(useInternet ? ST_INIT_NET : ST_IDLE);
        break;
    }
}

void Modem::doIdle(void) {
    uint32_t now = core.getTick();

    /* React to useInternet being flipped at runtime (e.g. PGN60 write from a
       panel, or the "internet" SMS command), roaming status changing (CREG
       polling), or allowRoaming being flipped (the "roaming" SMS command) —
       without waiting for a modem reboot. internetAllowed folds all three
       into one condition so the same teardown/init handling covers all of
       them; seed from the current value on first entry (doInit() already
       handled the boot-time case). */
    bool internetAllowed = useInternet && (!isRoaming || allowRoaming);
    static bool first = true;
    static bool prevInternetAllowed;
    static bool prevForce2gOnly;
    if (first) { first = false; prevInternetAllowed = internetAllowed; prevForce2gOnly = force2gOnly; }
    if (prevInternetAllowed != internetAllowed) {
        prevInternetAllowed = internetAllowed;
        if (!internetAllowed) {
            isInternetConnected = false;
            if (mqttConnected) { mqttTeardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }
            setState(ST_NET_TEARDOWN);
            return;
        } else {
            prevForce2gOnly = force2gOnly;  /* doInitNet() below reads it fresh anyway */
            setState(ST_INIT_NET);
            return;
        }
    }

    /* React to force2gOnly being flipped at runtime (PGN60 write from a
       panel, or the "2g" SMS command) while internet is already up — tear
       down and re-init the PDP context so the new AT+CNMP value actually
       takes effect now, instead of waiting for the next reboot/reconnect
       (doInitNet()'s step 0 only runs as part of that init sequence). Only
       meaningful while internetAllowed: if internet isn't up at all, the
       next time it comes up above already reads force2gOnly fresh. */
    if (internetAllowed && prevForce2gOnly != force2gOnly) {
        prevForce2gOnly = force2gOnly;
        isInternetConnected = false;
        netTeardownThenReinit = true;
        if (mqttConnected) { mqttTeardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }
        setState(ST_NET_TEARDOWN);
        return;
    } else if (!internetAllowed) {
        prevForce2gOnly = force2gOnly;
    }

    /* Internet dropped out from under an active MQTT session — tear it down;
       doCheckInternet()'s own self-heal will bring the PDP context back, and
       the retry timer below will restart MQTT once isInternetConnected again. */
    if (mqttConnected && !isInternetConnected) { mqttTeardownThenNet = true; setState(ST_MQTT_TEARDOWN); return; }

    /* mqttForceReconnect() was called (e.g. broker/password changed via SMS) —
       safe to act on now since doIdle() only ever runs from a clean top-level
       dispatch, never nested inside another state's handler. */
    if (mqttReconnectRequested) {
        mqttReconnectRequested = false;
        if (mqttConnected) { mqttTeardownThenNet = false; setState(ST_MQTT_TEARDOWN); return; }
        timerMqttRetry = now - 45000;  /* let the retry check below fire immediately */
    }

    if (smsNotifyPending) { smsNotifyPending = false; smsSlot = smsNotifySlot; setState(ST_READ_SMS); return; }

    /* Promote the oldest queued outgoing SMS into the active slot once it's
       free — safe here since doIdle() only ever runs from a clean top-level
       dispatch (see SmsQueueEntry comment in Modem.h). */
    if (!smsPending) {
        for (int i = 0; i < SMS_QUEUE_MAX; i++) {
            if (smsQueue[i].pending) {
                strncpy(smsPhone, smsQueue[i].phone, sizeof(smsPhone) - 1); smsPhone[sizeof(smsPhone)-1] = 0;
                strncpy(smsText,  smsQueue[i].text,  sizeof(smsText)  - 1); smsText[sizeof(smsText) -1] = 0;
                smsQueue[i].pending = false;
                smsPending = true;
                break;
            }
        }
    }
    if (smsPending)                  { setState(ST_SEND_SMS);  return; }
    if (ussdPending)                 { setState(ST_USSD);       return; }
    if ((now - timerCsq)  >= 30000) { setState(ST_POLL_CSQ);   return; }
    if ((now - timerCreg) >= 60000) { setState(ST_POLL_CREG);  return; }
    if (internetAllowed && (now - timerNet) >= 60000) {
        /* Already up — just re-verify (cheap: CGPADDR ± HTTP check). Down —
           go through the full CNMP/CGDCONT/CGATT/CGACT sequence again
           (ST_CHECK_INTERNET alone only re-checks CGPADDR; it can't bring a
           torn-down PDP context back up by itself). */
        setState(isInternetConnected ? ST_CHECK_INTERNET : ST_INIT_NET);
        return;
    }

    /* Safety net, not the primary path (that's smsNotifyPending, set
       directly off the +CMTI: URC) — every couple of minutes, ask the SIM
       directly whether anything unread is sitting in storage that we
       somehow never got notified about. Rare/cheap enough not to matter;
       see doPollSmsUnread(). */
    if ((now - timerSmsPoll) >= 120000) { setState(ST_POLL_SMS_UNREAD); return; }

    /* No point even trying without all three — CMQTTACCQ/CMQTTCONNECT just
       fail immediately on an empty client id/host anyway (confirmed on real
       hardware: "" client id gets a flat ERROR from the module, empty host
       gives a "tcp://:1883" URL that can never connect), so this would
       otherwise retry every 30s forever on a device that's simply never
       been configured yet — wasted airtime/CPU and, worse, extra time spent
       off ST_IDLE for no possible benefit. */
    bool mqttConfigured = mqttBroker[0] && mqttUsername[0] && mqttPassword[0];

    /* 45s, not 30 — confirmed on real hardware that 30s after the PDP
       context/internet genuinely came up wasn't always enough for the
       module's own TCP/DNS stack to settle: CMQTTCONNECT failed at the 30s
       mark but succeeded cleanly the next time, ~60s in. 45s split the
       difference; the retry-on-failure path below still covers it if this
       still isn't quite enough on a given attempt. */
    if (internetAllowed && isInternetConnected && !mqttConnected && mqttConfigured &&
        (now - timerMqttRetry) >= 45000) {
        timerMqttRetry = now;
        setState(ST_MQTT_START);
        return;
    }
    if (mqttConnected && mqttQueueHasPending()) { setState(ST_MQTT_PUB); return; }
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
        if (smsSlot >= 10) cmd[n++] = (char)('0' + smsSlot / 10);
        cmd[n++] = (char)('0' + smsSlot % 10);
        cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;

        if (atCmd(cmd, 5000)) {
            if ((answer & ANS_CMGR) && (answer & ANS_OK)) {
                log_info("SMS from "); log_info(cmgrPhone); log_info(": "); log_info(cmgrBody); log_info("\r\n");
                if (onSmsReceived) onSmsReceived(cmgrPhone, cmgrBody);
            }
            smsSlot = 0;
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
        for (int i = 0; smsPhone[i] && n < 33; i++) cmd[n++] = smsPhone[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;

        if (atCmd(cmd, 3000)) {
            if (answer & ANS_PROMPT) step++;
            else { smsPending = false; setState(ST_IDLE); }
        }
        break;
    }
    case 3: {
        /* Send body + CTRL-Z in one call — two separate transmit() calls
           would let the second overwrite the TX buffer before the first finishes */
        static char bodyBuf[143];
        uint8_t n = 0;
        while (smsText[n] && n < 140) { bodyBuf[n] = smsText[n]; n++; }
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
            smsPending = false;
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
                smsPending = false;
                setState(ST_IDLE);
            }
        } else if ((core.getTick()-t) >= 10000) {
            smsPending = false;
            setState(ST_IDLE);
        }
        break;
    }
}

void Modem::doPollCsq(void) {
    if (atCmd("AT+CSQ\r\n", 1000)) { timerCsq = core.getTick(); setState(ST_IDLE); }
}

void Modem::doPollCreg(void) {
    /* AT+CREG=2 enables lac/ci in the +CREG: response (and URC); cheap to
       re-assert every poll since some modems forget it across power cycles.
       AT+COPS=3,2 selects numeric operator format for the +COPS: query. */
    switch (step) {
    case 0: if (atCmd("AT+CREG=2\r\n",  300)) step++; break;
    case 1: if (atCmd("AT+CREG?\r\n",  2000)) step++; break;
    case 2: if (atCmd("AT+COPS=3,2\r\n", 300)) step++; break;
    case 3: if (atCmd("AT+COPS?\r\n",  3000)) step++; break;
    default:
        timerCreg = core.getTick();
        setState(ST_IDLE);
        break;
    }
}

void Modem::doPollSmsUnread(void) {
    /* Safety net for the CMTI-loss scenario smsNotifyPending already covers —
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
               routes into the existing smsNotifyPending -> ST_READ_SMS ->
               doReadSms() path — that already fetches the message properly
               by index and cleans up read messages via CMGD=1,2. */
            if (answer & ANS_CMGL) smsNotifyPending = true;
            timerSmsPoll = core.getTick();
            setState(ST_IDLE);
        }
        break;
    }
}

/* ── Mobile internet (active only when useInternet) ──────────────────────── */

void Modem::doInitNet(void) {
    /* Bring up a PDP context on whatever radio access is available — auto
       2G/4G (AT+CNMP=2) normally, or GSM-only (AT+CNMP=13) if force2gOnly
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
    bool isTelekomDe = !strcmp(operatorCode, "26201");
    const char* apnToUse  = apn[0]         ? apn         : (isTelekomDe ? "internet.telekom" : "");
    const char* userToUse = apnUsername[0] ? apnUsername : (isTelekomDe ? "telekom"          : "");
    const char* passToUse = apnPassword[0] ? apnPassword : (isTelekomDe ? "tm"                : "");

    switch (step) {
    case 0: if (atCmd(force2gOnly ? "AT+CNMP=13\r\n" : "AT+CNMP=2\r\n", 300)) step++; break;
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
        timerNet = core.getTick();
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
            /* Only trust ipAddress if +CGPADDR: actually arrived this round —
               on a timeout/error it would otherwise still hold a stale value
               from a previous successful check. */
            bool hasIp = (answer & ANS_CGPADDR) && ipAddress[0]
                       && strcmp(ipAddress, "0.0.0.0") != 0;
            if (!hasIp) {
                /* PDP context is down. Used to self-heal by jumping straight
                   back into ST_INIT_NET — but that bypasses ST_IDLE forever
                   while the network keeps rejecting CGACT (e.g. roaming with
                   allowRoaming=false: the network correctly refuses data
                   attach every time, and this node would hammer AT+CGACT in
                   a tight loop indefinitely). ST_IDLE is the only place that
                   re-checks internetAllowed (see doIdle()) and CREG/CSQ get
                   polled — going there first lets a no-longer-allowed
                   condition (roaming, useInternet toggled off, ...) actually
                   stop the retries instead of being permanently starved.
                   timerNet is refreshed here so doIdle()'s 60s gate paces
                   from *this* failure, not some earlier stale mark — without
                   it the gate could read as already-expired and refire on
                   every doIdle() pass instead of waiting a real 60s. doIdle()
                   itself now routes back into ST_INIT_NET (not straight into
                   ST_CHECK_INTERNET) when isInternetConnected is false, so
                   the PDP context actually gets re-provisioned instead of
                   just being re-polled forever. */
                isInternetConnected = false;
                timerNet = core.getTick();
                setState(ST_IDLE);
                return;
            }
            if (!internetCheckUrl[0]) {
                /* No check URL configured yet — PDP up is the best signal we have */
                if (!isInternetConnected) {
                    /* First time up (not just re-confirming on a later 60s
                       poll) — see the case 5 comment below for why this
                       matters. */
                    timerMqttRetry = core.getTick();
                }
                isInternetConnected = true;
                timerNet = core.getTick();
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
        for (int i = 0; internetCheckUrl[i] && n < 90; i++) cmd[n++] = internetCheckUrl[i];
        cmd[n++] = '"'; cmd[n++] = '\r'; cmd[n++] = '\n'; cmd[n] = 0;
        if (atCmd(cmd, 500)) step++;
        break;
    }
    case 3:
        /* This only confirms the command was accepted — the actual result
           arrives later as an asynchronous +HTTPACTION: URC (case 4). */
        if (atCmd("AT+HTTPACTION=0\r\n", 3000)) {
            if (answer & ANS_ERROR) { httpStatus = 0; step = 5; }
            else { answer &= ~ANS_HTTPACTION; t = core.getTick(); step++; }
        }
        break;
    case 4:
        if (answer & ANS_HTTPACTION) step++;
        else if ((core.getTick() - t) >= 20000) { httpStatus = 0; step++; }
        break;
    case 5:
        if (atCmd("AT+HTTPTERM\r\n", 2000)) {
            /* timerMqttRetry starts at 0 (see the constructor) and doIdle()
               gates the very first MQTT connect attempt on
               (now - timerMqttRetry) >= 45000 — without resetting it here,
               that window counts from raw power-on, not from when the PDP
               context actually came up. Confirmed on real hardware: when
               credentials got configured (via SMS) fast enough that a
               fixed delay-since-boot mark landed only ~10s after CGACT/
               CGPADDR succeeded, CMQTTSTART/CMQTTACCQ/CMQTTCONNECT all
               failed on the first attempt — but when credentials arrived
               much later (well past that mark), the same delay-since-boot
               gate had long since elapsed, so MQTT started immediately
               once configured, with the PDP context already settled for a
               while, and connected cleanly every time. Resetting here
               means the delay always counts from the moment internet
               genuinely became reachable, giving the module's stack the
               same settling time regardless of how soon credentials get
               configured after boot. Only on the false->true transition —
               a later periodic re-check succeeding again shouldn't push
               out a pending MQTT retry after an unrelated MQTT-side
               failure. */
            bool wasConnected = isInternetConnected;
            isInternetConnected = (httpStatus > 0 && httpStatus < 400);
            if (!wasConnected && isInternetConnected) timerMqttRetry = core.getTick();
            timerNet = core.getTick();
            setState(ST_IDLE);
        }
        break;
    }
}

void Modem::doNetTeardown(void) {
    if (atCmd("AT+CGACT=0,1\r\n", 15000)) {
        bool reinit = netTeardownThenReinit;
        netTeardownThenReinit = false;
        setState(reinit ? ST_INIT_NET : ST_IDLE);
    }
}

/* ── MQTT control channel (active only when useInternet && isInternetConnected) ── */

/* Enqueue "<mqttUsername>/cmd/actual/<name>" = payload for the next doMqttPub() pass.
   Coalescing: a name already queued just gets its value overwritten — only the
   latest value per name matters, there's no point re-sending stale intermediate
   ones. Safe to call regardless of mqttConnected; it'll drain once connected. */
void Modem::mqttPublish(const char* name, const char* payload) {
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++) {
        if (mqttPubQueue[i].name[0] && !strcmp(mqttPubQueue[i].name, name)) {
            strncpy(mqttPubQueue[i].value, payload, sizeof(mqttPubQueue[i].value)-1);
            mqttPubQueue[i].value[sizeof(mqttPubQueue[i].value)-1] = 0;
            mqttPubQueue[i].dirty = true;
            return;
        }
    }
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++) {
        if (!mqttPubQueue[i].name[0]) {
            strncpy(mqttPubQueue[i].name, name, sizeof(mqttPubQueue[i].name)-1);
            mqttPubQueue[i].name[sizeof(mqttPubQueue[i].name)-1] = 0;
            strncpy(mqttPubQueue[i].value, payload, sizeof(mqttPubQueue[i].value)-1);
            mqttPubQueue[i].value[sizeof(mqttPubQueue[i].value)-1] = 0;
            mqttPubQueue[i].dirty = true;
            return;
        }
    }
    /* queue full (all MQTT_PUB_MAX names in use) — drop silently */
}

bool Modem::mqttQueueHasPending(void) {
    for (uint8_t i = 0; i < MQTT_PUB_MAX; i++)
        if (mqttPubQueue[i].dirty) return true;
    return false;
}

/* Call after changing mqttBroker/mqttUsername/mqttPassword at runtime (e.g.
   the "server"/"password" SMS commands) so the new values take effect right
   away instead of waiting for the connection to drop on its own. Only sets a
   flag — see the mqttReconnectRequested comment in Modem.h for why this
   can't safely call setState() directly (may be invoked re-entrantly from
   inside onSmsReceived, itself called from doReadSms()). */
void Modem::mqttForceReconnect(void) {
    mqttReconnectRequested = true;
}

void Modem::doMqttStart(void) {
    static char     cmdBuf[144];
    static char     willTopicBuf[48];
    static uint32_t t = 0;
    const  int      cmdMax = (int)sizeof(cmdBuf) - 1;

    switch (step) {
    case 0: if (atCmd("AT+CMQTTSTART\r\n", 5000)) step++; break;
    case 1: {
        int n = 0;
        const char* pre = "AT+CMQTTACCQ=0,\"";
        while (*pre) cmdBuf[n++] = *pre++;
        for (const char* p = mqttUsername; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
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
    case 2: {
        int n = 0;
        const int topicMax = (int)sizeof(willTopicBuf) - 1;
        for (const char* p = mqttUsername; *p && n < topicMax; ) willTopicBuf[n++] = *p++;
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
    case 3:
        if (atCmd(willTopicBuf, 3000)) step++;
        break;
    case 4:
        /* len=1, qos=1, retain=1 — without retain, a client that (re)subscribes
           after the will already fired just gets the stale last retained "1"
           instead of the will's "0", masking the outage for anyone whose
           connection wasn't live at the exact moment the will published. */
        if (atCmd("AT+CMQTTWILLMSG=0,1,1,1\r\n", 2000)) step++;
        break;
    case 5:
        if (atCmd("0", 3000)) step++;
        break;
    case 6: {
        int n = 0;
        const char* pre = "AT+CMQTTCONNECT=0,\"tcp://";
        while (*pre) cmdBuf[n++] = *pre++;
        for (const char* p = mqttBroker; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        const char* mid = ":1883\",60,1,\"";
        while (*mid) cmdBuf[n++] = *mid++;
        for (const char* p = mqttUsername; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        cmdBuf[n++] = '"'; cmdBuf[n++] = ','; cmdBuf[n++] = '"';
        for (const char* p = mqttPassword; *p && n < cmdMax; ) cmdBuf[n++] = *p++;
        cmdBuf[n++] = '"'; cmdBuf[n++] = '\r'; cmdBuf[n++] = '\n'; cmdBuf[n] = 0;
        if (atCmd(cmdBuf, 3000)) { answer &= ~ANS_MQTT_URC; t = core.getTick(); step++; }
        break;
    }
    case 7:
        /* AT+CMQTTCONNECT is accepted immediately (OK); the real result
           arrives later as an asynchronous +CMQTTCONNECT: URC. */
        if (answer & ANS_MQTT_URC) step++;
        else if ((core.getTick() - t) >= 20000) { mqttUrcResult = 0xFF; step++; }
        break;
    default:
        if (mqttUrcResult == 0) {
            mqttPublish("online", "1");  /* announce ourselves once connected/subscribed */
            setState(ST_MQTT_SUB);
        } else {
            /* Connect failed — reset the modem's MQTT client state (DISC/REL/STOP)
               before retrying, otherwise the next CMQTTSTART/ACCQ/CONNECT just
               errors out forever because the old session is still considered live. */
            mqttTeardownThenNet = false;
            setState(ST_MQTT_TEARDOWN);
        }
        break;
    }
}

void Modem::doMqttSub(void) {
    static char     topicBuf[48];
    static char     cmdBuf[32];
    static uint32_t t = 0;

    switch (step) {
    case 0: {
        /* AT+CMQTTSUB, like AT+CMQTTTOPIC/PAYLOAD, takes a length and then
           expects exactly that many raw topic bytes — not an inline quoted
           string (that form returned a synchronous ERROR on this modem). */
        int n = 0;
        const int topicMax = (int)sizeof(topicBuf) - 1;
        for (const char* p = mqttUsername; *p && n < topicMax; ) topicBuf[n++] = *p++;
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
        if (atCmd(topicBuf, 3000)) { answer &= ~ANS_MQTT_URC; t = core.getTick(); step++; }
        break;
    case 2:
        if (answer & ANS_MQTT_URC) step++;
        else if ((core.getTick() - t) >= 10000) { mqttUrcResult = 0xFF; step++; }
        break;
    default:
        if (mqttUrcResult == 0) { mqttConnected = true; setState(ST_IDLE); }
        else {
            mqttTeardownThenNet = false;
            setState(ST_MQTT_TEARDOWN);
        }
        break;
    }
}

void Modem::doMqttPub(void) {
    static uint8_t  pubIdx = 0;
    static char     topicBuf[64];
    static char     cmdBuf[32];
    static uint32_t t = 0;
    static bool     pubFailed = false;   /* any step this attempt hit ANS_ERROR */
    static uint8_t  pubFailStreak = 0;   /* consecutive failed attempts */

    switch (step) {
    case 0: {
        bool found = false;
        for (uint8_t i = 0; i < MQTT_PUB_MAX; i++)
            if (mqttPubQueue[i].dirty) { pubIdx = i; found = true; break; }
        if (!found) { setState(ST_IDLE); return; }

        pubFailed = false;

        int n = 0;
        const int topicMax = (int)sizeof(topicBuf) - 1;
        for (const char* p = mqttUsername; *p && n < topicMax; ) topicBuf[n++] = *p++;
        const char* mid = "/cmd/actual/";
        for (const char* p = mid; *p && n < topicMax; ) topicBuf[n++] = *p++;
        for (const char* p = mqttPubQueue[pubIdx].name; *p && n < topicMax; ) topicBuf[n++] = *p++;
        topicBuf[n] = 0;

        int cn = 0;
        const char* pre = "AT+CMQTTTOPIC=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    }
    case 1:
        /* Raw topic bytes, no line terminator — the modem is waiting for
           exactly the byte count declared above, not an AT command line. */
        if (atCmd(topicBuf, 3000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    case 2: {
        int n = (int)strlen(mqttPubQueue[pubIdx].value);
        int cn = 0;
        const char* pre = "AT+CMQTTPAYLOAD=0,";
        while (*pre) cmdBuf[cn++] = *pre++;
        cn = appendUint(cmdBuf, cn, (uint16_t)n);
        cmdBuf[cn++] = '\r'; cmdBuf[cn++] = '\n'; cmdBuf[cn] = 0;
        if (atCmd(cmdBuf, 2000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    }
    case 3:
        if (atCmd(mqttPubQueue[pubIdx].value, 3000)) { if (answer & ANS_ERROR) pubFailed = true; step++; }
        break;
    case 4:
        /* <client_index>,<qos>,<pub_timeout>,<retained> — retained=1 so a late
           subscriber (or one that just reconnected) immediately gets the last
           known actual value instead of waiting for the next change. */
        if (atCmd("AT+CMQTTPUB=0,1,60,1\r\n", 3000)) {
            if (answer & ANS_ERROR) pubFailed = true;
            answer &= ~ANS_MQTT_URC; t = core.getTick(); step++;
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
        mqttPubQueue[pubIdx].dirty = false;

        /* Track consecutive publish failures — 5 in a row means the MQTT
           session is actually dead even though we never got a clean
           disconnect notice (e.g. the transport blipped silently, seen on
           real hardware: mosquitto itself logged nothing, yet publishes
           errored for a while). One success proves it's alive again right
           away. This doesn't itself force a reconnect — it just corrects
           mqttConnected to match reality — but doIdle()'s existing
           "!mqttConnected && internetAllowed && isInternetConnected, retry
           after 30s" check reacts to the same flag, so a real ST_MQTT_START
           retry does follow if the streak hits 5 and nothing else clears it
           first. That's the same recovery path any other disconnect already
           goes through, not new behavior introduced here. */
        if (pubFailed) {
            if (pubFailStreak < 5) pubFailStreak++;
            if (pubFailStreak >= 5) mqttConnected = false;
        } else {
            pubFailStreak = 0;
            mqttConnected = true;
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
       announce from (mqttConnected still reflects that — only cleared in
       this function's own final step below). If we're here to reset a
       failed/stuck connect attempt instead, there's nothing to say
       goodbye from — skip straight to the DISC/REL/STOP reset, same as
       before this feature existed. The Last Will (see doMqttStart) covers
       the case this function *doesn't* run at all — a crash/power-loss
       with no chance to tear down gracefully. */
    if (step == 0 && !mqttConnected) step = 3;

    switch (step) {
    case 0: {
        int n = 0;
        const int topicMax = (int)sizeof(topicBuf) - 1;
        for (const char* p = mqttUsername; *p && n < topicMax; ) topicBuf[n++] = *p++;
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
        if (!mqttConnected || atCmd("0", 3000)) step++;
        break;
    case 4:
        if (!mqttConnected) { step++; break; }
        if (atCmd("AT+CMQTTPUB=0,1,60,1\r\n", 3000)) { answer &= ~ANS_MQTT_URC; t = core.getTick(); step++; }
        break;
    case 5:
        if (!mqttConnected || (answer & ANS_MQTT_URC) || (core.getTick() - t) >= 15000) step++;
        break;
    case 6: if (atCmd("AT+CMQTTDISC=0,60\r\n", 5000)) step++; break;
    case 7: if (atCmd("AT+CMQTTREL=0\r\n",      3000)) step++; break;
    case 8: if (atCmd("AT+CMQTTSTOP\r\n",       3000)) step++; break;
    default:
        mqttConnected = false;
        /* mqttTeardownThenNet: true when called because useInternet was cleared
           or internet was lost (also tear down the PDP context); false when
           called to reset a stuck/failed MQTT session — internet stays up
           and timerMqttRetry will bring MQTT back on its own. */
        if (mqttTeardownThenNet) setState(ST_NET_TEARDOWN);
        else                     setState(ST_IDLE);
        break;
    }
}
