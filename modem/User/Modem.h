#ifndef MODEM_H
#define MODEM_H

#include "n32wb452.h"
#include "usart.h"
#include "gpio.h"
#include <stdint.h>

class Modem {
public:
    /* ── Public state ────────────────────────────────────────────────── */
    bool    isRegistered;   /* +CREG: 1  — on home network               */
    bool    isRoaming;      /* +CREG: 5  — roaming                       */
    uint8_t csq;            /* 0-31 = valid, 0xFF = unknown              */
    char    imei[16];
    char    iccid[21];
    char    ownNumber[16];

    /* Last SMS seen in each direction — exposed for STRID_LAST_*_SMS_* */
    char    smsPhone[16];   /* last outgoing SMS recipient   */
    char    smsText[141];   /* last outgoing SMS body        */
    char    cmgrPhone[20];  /* last incoming SMS sender      */
    char    cmgrBody[161];  /* last incoming SMS body        */

    /* Network info — from +CREG: (lac/ci) and +COPS: (operator), polled  */
    char     operatorCode[7];  /* numeric MCC+MNC, e.g. "25099"           */
    char     operatorName[24]; /* resolved carrier name, empty if unknown */
    uint16_t lac;               /* location area code                     */
    uint32_t cellId;            /* cell ID                                */

    /* Access control — persisted in flash */
    char       phones[5][16];  /* [0] = admin phone; [1..4] = trusted phones */
    char       pin[5];         /* 4-digit PIN, null-terminated               */
    bool       isOnlySmsMode;
    uint8_t    tempUnit;       /* 0 = °C, 1 = °F — persisted in flash        */
    bool       faultReport;    /* send SMS on fault — persisted in flash      */
    bool       cmdAck;         /* send confirmation on device commands        */
    uint8_t    language;       /* 0 = English, 1 = German — persisted in flash;
                                   used to reply when the SMS itself carries no
                                   language cue (parse errors, bare "?")       */

    /* Called on every received SMS (phone and text are temporary buffers) */
    void (*onSmsReceived)(const char* phone, const char* text);

    /* ── API ─────────────────────────────────────────────────────────── */
    Modem();
    void initialize(void);
    void handler(void);
    void sendSms(const char* phone, const char* text);
    void sendUssd(const char* req);  /* send USSD request, log reply to terminal */

    bool smsDebugMode;   /* true = log SMS to terminal, skip real sending */

    /* Called from USART TX interrupt */
    void txIsr(void);

    Usart_C usart;   /* public: accessed from USART1_IRQHandler in Modem.cpp */

private:
    Gpio_C  PowerkeyPin;
    Gpio_C  PowergoodPin;
    Gpio_C  DTRPin;

    /* ── Answer flags (set by parseLine) ─────────────────────────────── */
    enum {
        ANS_OK      = 1<<0,
        ANS_ERROR   = 1<<1,
        ANS_TIMEOUT = 1<<2,
        ANS_READY   = 1<<3,   /* "Call Ready" / "SMS Ready" / "RDY"     */
        ANS_CSQ     = 1<<4,   /* +CSQ: — csq field updated directly      */
        ANS_CREG    = 1<<5,   /* +CREG: — isRegistered/isRoaming updated */
        ANS_CMTI    = 1<<6,   /* +CMTI: — new SMS arrived, smsSlot set   */
        ANS_CMGR    = 1<<7,   /* +CMGR: — SMS header parsed              */
        ANS_CMGS    = 1<<8,   /* +CMGS: — SMS sent confirmation          */
        ANS_ICCID   = 1<<9,   /* +ICCID: — iccid field updated           */
        ANS_CNUM    = 1<<10,  /* +CNUM: — ownNumber updated              */
        ANS_PROMPT  = 1<<11,  /* >  — modem ready for SMS body           */
        ANS_CUSD    = 1<<12,  /* +CUSD: — USSD response received         */
        ANS_COPS    = 1<<13,  /* +COPS: — operatorCode updated           */
    };
    uint32_t answer;

    /* ── Capture mode for multi-line responses ───────────────────────── */
    enum CaptureMode { CAP_NONE, CAP_IMEI, CAP_CMGR_BODY };
    CaptureMode capture;

    /* ── State machine ───────────────────────────────────────────────── */
    enum State {
        ST_POWER_ON,
        ST_WAIT_READY,
        ST_INIT,
        ST_IDLE,
        ST_READ_SMS,
        ST_SEND_SMS,
        ST_POLL_CSQ,
        ST_POLL_CREG,
        ST_USSD,
    };
    State   state;
    int8_t  step;

    void setState(State s) { state = s; step = 0; answer = 0; capture = CAP_NONE; }

    /* ── Outgoing SMS (single pending slot) ──────────────────────────── */
    bool    smsPending;

    /* ── Incoming SMS ────────────────────────────────────────────────── */
    uint8_t smsSlot;

    /* ── USSD ────────────────────────────────────────────────────────── */
    bool     ussdPending;
    char     ussdReq[32];

    /* ── Polling timers ──────────────────────────────────────────────── */
    uint32_t timerCsq;
    uint32_t timerCreg;

    /* ── RX line accumulator ─────────────────────────────────────────── */
    uint16_t rxCursor;
    static const uint16_t LINE_SIZE = 256;
    char     lineBuf[LINE_SIZE];
    uint16_t lineLen;

    /* ── Internal methods ────────────────────────────────────────────── */
    bool  atCmd(const char* cmd, uint32_t ms);
    void  transmit(const char* s);
    void  drainRx(void);
    void  parseLine(void);

    void  doPowerOn(void);
    void  doWaitReady(void);
    void  doInit(void);
    void  doIdle(void);
    void  doReadSms(void);
    void  doSendSms(void);
    void  doPollCsq(void);
    void  doPollCreg(void);
    void  doUssd(void);
};

extern Modem modem;

#endif /* MODEM_H */
