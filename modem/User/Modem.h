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
    uint8_t  networkAcT;        /* 3GPP <AcT> from +COPS: (0=GSM,2=UTRAN,
                                    7=E-UTRAN,...), 0xFF = unknown/not yet
                                    queried — real tech, sent to the panel
                                    for display (see canBroadcast()) */

    /* Mobile internet (PDP context), active only when useInternet          */
    bool     isInternetConnected; /* PDP up and (if configured) HTTP check passed */
    char     ipAddress[16];       /* from +CGPADDR, for diagnostics        */
    char     internetCheckUrl[64];/* HTTP GET target used to verify real connectivity;
                                      STRID_INTERNET_CHECK_URL, RAM-only for now */

    /* MQTT control channel, active only when useInternet && isInternetConnected.
       Broker/username/password are persisted in flash (flash.cpp) and can be
       changed at runtime via the "server"/"password" SMS commands (or CAN/
       StringTransfer) — call mqttForceReconnect() after changing them. */
    bool     mqttConnected;
    char     mqttBroker[32];     /* host only, port fixed at 1883; STRID_MQTT_BROKER */
    char     mqttUsername[16];  /* STRID_MODEM_LOGIN — also the topic namespace       */
    char     mqttPassword[24];  /* STRID_MODEM_PASSWORD                                */

    /* Called when "<mqttUsername>/cmd/desired/<name>" arrives (name = last path segment) */
    void (*onMqttCommand)(const char* name, const char* payload);

    /* Access control — persisted in flash */
    char       phones[5][16];  /* [0] = admin phone; [1..4] = trusted phones */
    char       pin[5];         /* 4-digit PIN, null-terminated               */
    bool       useInternet;    /* true = use mobile internet/MQTT, false = SMS-only.
                                   CAN wire bit (PGN60) and the flash byte format keep
                                   the old "onlySmsMode" polarity — translated at the
                                   two boundaries (Timberline.cpp CAN read, flash.cpp)
                                   so this rename doesn't touch existing wire/storage
                                   formats or need a migration on already-flashed units. */
    uint8_t    tempUnit;       /* 0 = °C, 1 = °F — persisted in flash        */
    bool       allowRoaming;   /* false (default) = tear down/never bring up mobile
                                   internet/MQTT while isRoaming; true = allowed.
                                   Persisted in flash — see the "roaming" SMS command
                                   (Library/Sms/timberline_sms.cpp) and doIdle()'s
                                   internetAllowed gate below.                   */
    bool       force2gOnly;    /* false (default) = AT+CNMP=2 (automatic 2G/4G);
                                   true = AT+CNMP=13 (GSM-only) in doInitNet().
                                   Persisted in flash — see the "2g" SMS command. */
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
    void mqttPublish(const char* name, const char* payload);  /* enqueue "cmd/actual/<name>" */
    void mqttForceReconnect(void);  /* call after mqttBroker/mqttPassword changes at runtime */

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
        ANS_CGPADDR = 1<<14,  /* +CGPADDR: — ipAddress updated           */
        ANS_HTTPACTION = 1<<15, /* +HTTPACTION: — httpStatus updated, URC */
        ANS_MQTT_URC = 1<<16, /* +CMQTTCONNECT:/+CMQTTSUB:/+CMQTTPUB: — mqttUrcResult updated */
        ANS_CMGL    = 1<<17,  /* +CMGL: — at least one line seen, smsSlot set (see ST_POLL_SMS_UNREAD) */
    };
    uint32_t answer;

    /* ── Capture mode for multi-line responses ───────────────────────── */
    enum CaptureMode { CAP_NONE, CAP_IMEI, CAP_CMGR_BODY, CAP_MQTT_TOPIC, CAP_MQTT_PAYLOAD };
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
        ST_POLL_SMS_UNREAD,
        ST_USSD,
        ST_INIT_NET,
        ST_CHECK_INTERNET,
        ST_NET_TEARDOWN,
        ST_MQTT_START,
        ST_MQTT_SUB,
        ST_MQTT_PUB,
        ST_MQTT_TEARDOWN,
    };
    State   state;
    int8_t  step;

    void setState(State s) { state = s; step = 0; answer = 0; capture = CAP_NONE; }

    /* ── Outgoing SMS ────────────────────────────────────────────────── */
    bool    smsPending;   /* true while smsPhone/smsText is the one actively being sent */

    /* Overflow queue for sendSms() calls that arrive while smsPending is
       already true — e.g. a combined SMS ("server X,login Y,password
       Z,getlink") dispatches multiple command handlers in one synchronous
       pass, each calling sendSms() with its own reply. Without this, every
       call after the first used to be silently dropped (smsPending was
       already true), so only the first command's confirmation ever went
       out. doIdle() promotes the oldest queued entry into
       smsPhone/smsText once the active send finishes. */
    enum { SMS_QUEUE_MAX = 4 };
    struct SmsQueueEntry { char phone[16]; char text[141]; bool pending; };
    SmsQueueEntry smsQueue[SMS_QUEUE_MAX];

    /* ── Incoming SMS ────────────────────────────────────────────────── */
    /* The slot doReadSms() is actively working on right now — set exactly
       once, by doIdle(), the moment it starts a read (copied from
       smsNotifySlot below). doReadSms()'s own command-building re-reads
       this on every call while its step is in flight; it must NOT change
       out from under an in-progress read, or the AT+CMGR=<smsSlot> command
       string changes mid-wait, atCmd() sees a new hash, and abandons the
       response it was already waiting for (confirmed on real hardware: a
       second +CMTI: arriving while CMGR=1 was still in flight caused the
       firmware to fire CMGR=2 immediately, and CMGR=1's late response then
       landed as a stray, out-of-place +CMGR: line later on, eventually
       producing a bogus CMGR=0 / "CMS ERROR: Invalid memory index"). */
    uint8_t smsSlot;
    /* Slot most recently announced by "+CMTI:"/found by ST_POLL_SMS_UNREAD —
       safe to overwrite any time, from anywhere, since nothing reads it
       while a read is in flight; only doIdle() copies it into smsSlot. */
    uint8_t smsNotifySlot;
    /* Set the moment "+CMTI:" is parsed, independent of the `answer` bitmask
       (see ANS_CMTI) — `answer` gets wiped to 0 by atCmd() the instant any
       OTHER command is issued (new hash), which happens constantly while
       doMqttStart()/doInitNet()/etc. cycle through their own steps. If
       "there's an SMS waiting" only lived in `answer`, a notification that
       arrives while the modem is busy with anything else would be silently
       lost the moment the busy state's next AT command goes out — confirmed
       happening in practice when MQTT can't connect and retries in a tight
       loop for minutes at a time. This flag survives that; only doIdle()
       clears it, once it actually acts on it. */
    bool    smsNotifyPending;

    /* ── USSD ────────────────────────────────────────────────────────── */
    bool     ussdPending;
    char     ussdReq[32];

    /* ── Polling timers ──────────────────────────────────────────────── */
    uint32_t timerCsq;
    uint32_t timerCreg;
    uint32_t timerNet;
    uint32_t timerMqttRetry;
    uint32_t timerSmsPoll; /* fallback safety net — see ST_POLL_SMS_UNREAD */

    /* ── Internet check scratch ──────────────────────────────────────── */
    uint16_t httpStatus;

    /* ── MQTT scratch ─────────────────────────────────────────────────── */
    uint8_t  mqttUrcResult;         /* result code from +CMQTTCONNECT:/+CMQTTSUB:/+CMQTTPUB: */
    char     mqttRxName[16];        /* last path segment of an incoming desired-topic        */
    char     mqttRxPayload[8];
    bool     mqttTeardownThenNet;   /* ST_MQTT_TEARDOWN's next hop: ST_NET_TEARDOWN (true) or
                                        back to ST_IDLE to retry MQTT alone (false)             */
    bool     netTeardownThenReinit; /* ST_NET_TEARDOWN's next hop: ST_INIT_NET (true, so a live
                                        force2gOnly change actually re-issues AT+CNMP with the
                                        new value) or back to ST_IDLE as usual (false)           */
    bool     mqttReconnectRequested; /* set by mqttForceReconnect(), consumed by doIdle() —
                                         never setState() directly there: mqttForceReconnect()
                                         can be called re-entrantly from inside onSmsReceived,
                                         itself invoked from doReadSms(), and clobbering
                                         state/step out from under the caller's own state
                                         handler would corrupt the state machine.            */

    /* A name, once assigned a slot by mqttPublish(), keeps that slot for
       the device's entire runtime (matched by name, never freed) — so this
       must exceed the total number of *distinct* topic names the firmware
       will ever publish, not just how many can be dirty at once (though
       those happen to coincide today: justConnected in Timberline.cpp
       forces every mqttActualizerHandler field dirty on one pass, which is
       the same worst case). Currently ~50: mqttActualizerHandler's ~47
       (control mirrors + zn<N>/* + errors) + "telemetry" + "linkToken" +
       "online" (published once per connect from doMqttStart(); the "0"
       farewell in doMqttTeardown() is a one-off AT sequence, not queued
       through here). A
       full queue silently drops whatever doesn't fit. Kept with headroom
       above that count since it's cheap (a few dozen bytes per extra slot)
       and this count only ever grows as more fields get wired up. */
    enum { MQTT_PUB_MAX = 56 };
    struct MqttPubEntry {
        char name[16];
        /* Must fit the largest single payload: the "errors" CSV topic's
           worst case (8 codes × 3 digits + 7 commas + null = 32) is
           currently the biggest, just ahead of "telemetry"'s 24-char
           base64 + null and the 16-hex-char "getlink" token + null. */
        char value[36];
        bool dirty;
    };
    MqttPubEntry mqttPubQueue[MQTT_PUB_MAX];
    bool  mqttQueueHasPending(void);

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
    void  doPollSmsUnread(void);
    void  doUssd(void);
    void  doInitNet(void);
    void  doCheckInternet(void);
    void  doNetTeardown(void);
    void  doMqttStart(void);
    void  doMqttSub(void);
    void  doMqttPub(void);
    void  doMqttTeardown(void);
};

extern Modem modem;

#endif /* MODEM_H */
