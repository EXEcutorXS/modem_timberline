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
    int8_t  csq;            /* 0-31, -1 = unknown                        */
    char    imei[16];
    char    iccid[21];
    char    ownNumber[16];

    /* Access control — persisted in flash */
    char    phones[5][16];  /* [0] = admin phone; [1..4] = trusted phones */
    char    pin[5];         /* 4-digit PIN, null-terminated               */
    bool    isOnlySmsMode;

    /* Called on every received SMS (phone and text are temporary buffers) */
    void (*onSmsReceived)(const char* phone, const char* text);

    /* ── API ─────────────────────────────────────────────────────────── */
    Modem();
    void initialize(void);
    void handler(void);
    void sendSms(const char* phone, const char* text);

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
    };
    State   state;
    int8_t  step;

    void setState(State s) { state = s; step = 0; answer = 0; capture = CAP_NONE; }

    /* ── Outgoing SMS (single pending slot) ──────────────────────────── */
    bool    smsPending;
    char    smsPhone[16];
    char    smsText[141];

    /* ── Incoming SMS ────────────────────────────────────────────────── */
    uint8_t smsSlot;
    char    cmgrPhone[20];
    char    cmgrBody[161];

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
};

extern Modem modem;
extern uint8_t versionHardware;

#endif /* MODEM_H */
