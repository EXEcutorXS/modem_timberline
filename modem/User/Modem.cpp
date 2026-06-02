#include "Modem.h"
#include "core.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

uint8_t versionHardware = 0;
Modem   modem;


extern "C" void USART1_IRQHandler(void) {
    if (USART_GetIntStatus(USART1, USART_INT_RXDNE) != RESET)
        modem.usart.receiveIntHandler((uint8_t)USART_ReceiveData(USART1));
    if (USART_GetIntStatus(USART1, USART_INT_TXDE) != RESET)
        modem.txIsr();
}

/* ── Constructor ─────────────────────────────────────────────────────── */
Modem::Modem()
    : isRegistered(false), isRoaming(false), csq(-1),
      onSmsReceived(0),
      answer(0), capture(CAP_NONE),
      state(ST_POWER_ON), step(0),
      smsPending(false), smsSlot(0),
      ussdPending(false),
      timerCsq(0), timerCreg(0),
      rxCursor(0), lineLen(0),
      smsDebugMode(true),
      isOnlySmsMode(true),
      tempUnit(0),
      faultReport(true),
      cmdAck(true)
{
    imei[0] = iccid[0] = ownNumber[0] = 0;
    smsPhone[0] = smsText[0] = cmgrPhone[0] = cmgrBody[0] = ussdReq[0] = 0;
    for (int i = 0; i < 5; i++) phones[i][0] = 0;
    pin[0]='1'; pin[1]='2'; pin[2]='3'; pin[3]='4'; pin[4]='\0';
}

void Modem::txIsr(void) { usart.transmitNextByte(); }

/* ── initialize ──────────────────────────────────────────────────────── */
void Modem::initialize(void) {
    usart.initialize(1, 115200);
    PowergoodPin.Initialize(GPIOA, GPIO_PIN_3, GPIO_Mode_IPU);
    DTRPin.Initialize(GPIOB, GPIO_PIN_1, GPIO_Mode_Out_PP);
    DTRPin.Reset();
    if (versionHardware == 1)
        PowerkeyPin.Initialize(GPIOB, GPIO_PIN_3, GPIO_Mode_Out_PP);
    else
        PowerkeyPin.Initialize(GPIOB, GPIO_PIN_7, GPIO_Mode_Out_PP);
    PowerkeyPin.Reset();
}

/* ── handler ─────────────────────────────────────────────────────────── */
void Modem::handler(void) {
    if (!PowergoodPin.Get()) return;
    drainRx();
    switch (state) {
        case ST_POWER_ON:   doPowerOn();   break;
        case ST_WAIT_READY: doWaitReady(); break;
        case ST_INIT:       doInit();      break;
        case ST_IDLE:       doIdle();      break;
        case ST_READ_SMS:   doReadSms();   break;
        case ST_SEND_SMS:   doSendSms();   break;
        case ST_POLL_CSQ:   doPollCsq();   break;
        case ST_POLL_CREG:  doPollCreg();  break;
        case ST_USSD:       doUssd();      break;
    }
}

/* ── sendSms ─────────────────────────────────────────────────────────── */
void Modem::sendSms(const char* phone, const char* text) {
    if (!phone || phone[0] != '+') return;

    log_info("[SMS] to: "); log_info(phone);
    log_info(" | ");        log_info(text);
    log_info("\r\n");

    if (smsDebugMode) return;

    if (smsPending) return;
    strncpy(smsPhone, phone, sizeof(smsPhone) - 1); smsPhone[sizeof(smsPhone)-1] = 0;
    strncpy(smsText,  text,  sizeof(smsText)  - 1); smsText[sizeof(smsText) -1] = 0;
    smsPending = true;
}

/* ═══════════════════════════════════════════════════════════════════════
   AT engine
   ═══════════════════════════════════════════════════════════════════════*/

void Modem::transmit(const char* s) {
    static char buf[512];
    uint16_t n = 0;
    while (*s && n < 511) buf[n++] = *s++;
    if (!n) return;
    usart.send((uint8_t*)buf, n);
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
    while (rxCursor != usart.getBufferPos()) {
        char c = (char)usart.getByte(rxCursor++);
        if (rxCursor >= Usart_C::BUFFER_SIZE) rxCursor = 0;

        char dbg[2] = {c, 0}; log_at(dbg);

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
        capture = CAP_NONE;
        if (lineLen >= 14) { strncpy(imei, s, sizeof(imei)-1); imei[sizeof(imei)-1] = 0; }
        return;
    }
    if (capture == CAP_CMGR_BODY) {
        capture = CAP_NONE;
        strncpy(cmgrBody, s, sizeof(cmgrBody)-1); cmgrBody[sizeof(cmgrBody)-1] = 0;
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
        csq = (int8_t)atoi(s + 6);
        answer |= ANS_CSQ;
    }
    else if (starts(s,"+CREG: ")) {
        char stat = '0';
        for (int i = 7; s[i]; i++) if (s[i] >= '0' && s[i] <= '5') stat = s[i];
        isRegistered = (stat == '1');
        isRoaming    = (stat == '5');
        answer |= ANS_CREG;
    }
    else if (starts(s,"+CMTI: ")) {
        /* +CMTI: "SM",3 */
        const char* p = s + 7;
        while (*p && *p != ',') p++;
        smsSlot = (*p == ',') ? (uint8_t)(*(p+1) - '0') : 1;
        if (smsSlot == 0) smsSlot = 1;
        answer |= ANS_CMTI;
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
        /* +CUSD: 0,"text",15  — log the quoted text field */
        char cusdBuf[64];
        nthQuoted(s + 7, 1, cusdBuf, sizeof(cusdBuf));
        log_info("[USSD] "); log_info(cusdBuf); log_info("\r\n");
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
    case 4:  if (atCmd("AT+CMGD=,4\r\n",          5000)) step++; break;
    case 5:  if (atCmd("AT+CNMI=2,1,0,0,0\r\n",    300)) step++; break;
    case 6:  if (atCmd("AT+CSQ\r\n",              1000)) step++; break;
    case 7:  if (atCmd("AT+CICCID\r\n",           5000)) step++; break;
    case 8:
        /* IMEI arrives as a plain line before OK — use capture mode */
        capture = CAP_IMEI;
        if (atCmd("AT+CGSN\r\n", 3000)) { capture = CAP_NONE; step++; }
        break;
    case 9:  if (atCmd("AT+CNUM\r\n",             3000)) step++; break;
    case 10: if (atCmd("AT+CREG?\r\n",            2000)) step++; break;
    default:
        log_info("Modem ready. IMEI="); log_info(imei[0]       ? imei       : "?");
        log_info(" SIM=");              log_info(ownNumber[0]   ? ownNumber  : "?");
        log_info("\r\n");
        timerCsq = timerCreg = core.getTick();
        setState(ST_IDLE);
        break;
    }
}

void Modem::doIdle(void) {
    uint32_t now = core.getTick();
    if (answer & ANS_CMTI)          { answer &= ~ANS_CMTI; setState(ST_READ_SMS);  return; }
    if (smsPending)                  { setState(ST_SEND_SMS);  return; }
    if (ussdPending)                 { setState(ST_USSD);       return; }
    if ((now - timerCsq)  >= 30000) { setState(ST_POLL_CSQ);   return; }
    if ((now - timerCreg) >= 60000) { setState(ST_POLL_CREG);  return; }
}

/* ── sendUssd ────────────────────────────────────────────────────────── */
void Modem::sendUssd(const char* req) {
    if (!req || !req[0] || ussdPending) return;
    strncpy(ussdReq, req, sizeof(ussdReq) - 1);
    ussdReq[sizeof(ussdReq) - 1] = 0;
    ussdPending = true;
}

/* ── doUssd ──────────────────────────────────────────────────────────── */
void Modem::doUssd(void) {
    static char cmd[48];

    switch (step) {
    case 0:
        if (atCmd("AT+CSCS=\"IRA\"\r\n", 300)) step++;
        break;
    case 1: {
        /* Build AT+CUSD=1,"<req>",15\r\n */
        int n = 0;
        const char* pre = "AT+CUSD=1,\"";
        while (*pre) cmd[n++] = *pre++;
        for (int i = 0; ussdReq[i] && n < 44; i++) cmd[n++] = ussdReq[i];
        cmd[n++]='"'; cmd[n++]=','; cmd[n++]='1'; cmd[n++]='5';
        cmd[n++]='\r'; cmd[n++]='\n'; cmd[n]=0;

        if (atCmd(cmd, 10000)) {
            if (answer & ANS_CUSD) {
                /* Response already logged by parseLine */
            } else if (answer & ANS_TIMEOUT) {
                log_info("[USSD] timeout\r\n");
            } else if (answer & ANS_ERROR) {
                log_info("[USSD] error\r\n");
            }
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

    switch (step) {
    case 0:
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
        if ((answer & (ANS_OK | ANS_ERROR)) || (core.getTick()-t) >= 10000) {
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
    if (atCmd("AT+CREG?\r\n", 2000)) { timerCreg = core.getTick(); setState(ST_IDLE); }
}
