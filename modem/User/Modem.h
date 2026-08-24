#ifndef MODEM_H
#define MODEM_H

#include "n32wb452.h"
#include "usart.h"
#include "gpio.h"
#include <stdint.h>

/* Kept in sync with flash.h's FLASH_OTA_PAGE_COUNT/FLASH_PAGE_SIZE — see the
   static_asserts in Modem.cpp (this header intentionally doesn't include
   flash.h itself, to keep it out of Modem.h's public include surface). */
#define MODEM_OTA_PAGE_SIZE   2048
#define MODEM_OTA_PAGE_COUNT  80

/* Baked-in fallback broker — applied by flash.cpp's sanitizeString() when
   flash has never held a real value (genuinely erased, factory-fresh
   flash), and mirrored in the Modem constructor below for the in-RAM
   value before readSetup() runs. Lets a device auto-register (see
   startAutoRegister()) without ever having been told a broker via SMS —
   "server <host>" still overrides it same as always. One default for now;
   picking between several per-region defaults is future work. */
#define MODEM_DEFAULT_BROKER "multihot.duckdns.org"

class Modem {
public:
    /* ── Network / registration state ────────────────────────────────── */
    struct NetworkState {
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

        /* From +CREG: (lac/ci) and +COPS: (operator), polled */
        char     operatorCode[7];  /* numeric MCC+MNC, e.g. "25099"           */
        char     operatorName[24]; /* resolved carrier name, empty if unknown */
        uint16_t lac;               /* location area code                     */
        uint32_t cellId;            /* cell ID                                */
        uint8_t  networkAcT;        /* 3GPP <AcT> from +COPS: (0=GSM,2=UTRAN,
                                        7=E-UTRAN,...), 0xFF = unknown/not yet
                                        queried — real tech, sent to the panel
                                        for display (see canBroadcast()) */
    } network;

    /* ── Mobile internet (PDP context), active only when config.useInternet ── */
    struct InternetState {
        bool     isInternetConnected; /* PDP up and (if configured) HTTP check passed */
        char     ipAddress[16];       /* from +CGPADDR, for diagnostics        */
        char     internetCheckUrl[64];/* HTTP GET target used to verify real connectivity;
                                          STRID_INTERNET_CHECK_URL, persisted (flash.cpp) */
        char     connectionLink[96];  /* Last "getlink" URL (see TL_CMD_GETLINK in
                                          Timberline.cpp) — STRID_CONNECTION_LINK, persisted
                                          (flash.cpp) so it survives a reboot even though it's
                                          only regenerated rarely (on demand, via SMS). */
        char     apn[32];             /* Explicit PDP context APN override, set via the "apn"
                                          SMS command; empty = auto (blank CGDCONT APN, or a
                                          recognized-operator default — see doInitNet()).
                                          Persisted in flash so a value found by trial on a
                                          remote device survives a power cycle. */
        char     apnUsername[32];     /* PDP auth username (AT+CGAUTH), set via "apnuser";
                                          empty = no auth, unless a recognized operator needs
                                          one (see doInitNet()). Persisted in flash. */
        char     apnPassword[32];     /* PDP auth password (AT+CGAUTH), set via "apnpass".
                                          Persisted in flash. */
    } internet;

    /* ── MQTT control channel, active only when config.useInternet &&
       internet.isInternetConnected. broker/username/password are persisted
       in flash (flash.cpp) and can be changed at runtime via the "server"/
       "password" SMS commands (or CAN/StringTransfer) — call
       mqttForceReconnect() after changing them. ─────────────────────────── */
    struct MqttState {
        bool     connected;
        char     broker[32];    /* host only, port fixed at 1883; STRID_MQTT_BROKER */
        char     username[16];  /* STRID_MODEM_LOGIN — also the topic namespace       */
        char     password[24];  /* STRID_MODEM_PASSWORD                                */
        uint8_t  telemetryIntervalSec; /* How often Timberline::mqttTelemetryHandler()
                                          publishes, 5-60s, default 15. Modem-local
                                          behavior (not a heater/CAN setting), so it lives
                                          here, not in Timberline — set via cmd/desired/
                                          telemetryInt (see onMqttCommandReceived() in
                                          Timberline.cpp); RAM-only, not persisted in flash
                                          (the web app/broker is the source of truth — a
                                          retained MQTT message re-delivers it on reboot). */
    } mqtt;

    /* MBC-2 firmware OTA — see doOta() in Modem.cpp. Downloads
       firmware/<version>/{firmware.crc32,firmware.bin} from the same host
       as mqtt.broker over plain HTTP (AT+HTTPREAD), stages it page-by-page
       in this modem's own spare flash (FLASH_OTA_BUF_ADDR, see flash.h),
       verifying each page before writing it. The actual CAN relay onto
       MBC-2 is a separate, later phase (not implemented here). */
    enum OtaStatus { OTA_IDLE, OTA_STAGING, OTA_DONE, OTA_ERROR };
    struct OtaState {
        OtaStatus status;
        uint16_t  page;       /* current page index, for progress reporting */
        uint16_t  pageTotal;  /* total pages for the image being staged     */
        /* What's actually sitting in the flash OTA buffer right now —
           independent of status/page/pageTotal above, which only describe
           a download *in progress*. Loaded once at boot (see initialize())
           and refreshed whenever a download finishes (see doOta()'s
           cleanup step), from FLASH_OTA_META_ADDR — see flash.h. This is
           the value the web panel is meant to show the user before they
           decide to relay it onto MBC-2 over CAN: "this is the firmware
           the modem is currently holding," separate from "a download just
           finished." stagedValid false means stagedVersion/stagedBytes are
           blank — nothing verified is staged (fresh device, or the sector
           was erased/corrupt). */
        bool      stagedValid;
        char      stagedVersion[24];
        uint32_t  stagedBytes;

        /* Which device type this staged image is for, and the flash layout
           to use when relaying it over CAN (see doCanRelay() in
           Timberline.cpp) — fetched from "/firmware/<type>/profile.txt" by
           doFetchProfile() right before the firmware download itself (see
           ST_FETCH_PROFILE). Replaces the old MBC-2-only hardcoded
           MBC2_APP_FLASH_BASE/sector-5-then-6 sequence, so a new device
           type only needs a new profile.txt on the server, no modem
           reflash. RAM-only, not persisted to flash alongside
           stagedVersion/stagedBytes above — a reboot between "staged" and
           "relay" loses this and needs a fresh otaStart (which re-fetches
           the profile anyway), same as any other in-progress state this
           firmware doesn't carry across a reset. */
        uint8_t   deviceType;
        uint32_t  flashBase;
        uint8_t   eraseSectors[4];
        uint8_t   eraseSectorCount;
    } ota;
    void startOta(uint8_t deviceType, const char* version);  /* called from onMqttCommandReceived() */

    /* Bootloader "algorithm" table — which PGN105/106 command sequence a
       given bootloader version actually supports safely, since not every
       bootloader out there behaves the same (some are known unstable and
       must never be used for OTA). Fetched from "/bootloaders.txt" (the
       whole table, not per-device — bootloader identity is independent of
       which device type it's flashing) right alongside profile.txt, see
       doFetchProfile(). Timberline::doCanRelay() looks up the bootloader's
       own announced version (from its PGN=18 reply, see
       ProcessCanMessage()'s case 123) via lookupBootloaderAlgorithm() once
       it's detected, before doing anything destructive:
         algorithm 0 (not found in the table) or 1 (known unstable, never
           safe for OTA) — refuse, fail the relay immediately.
         algorithm 2 — the current doCanRelay() sequence (set-address/erase/
           flash/verify via PGN105 sub0-7), safe on every bootloader tested
           so far.
       A missing/unreachable bootloaders.txt does NOT fail the OTA download
       itself (unlike profile.txt) — it just leaves the table empty, so any
       later CAN relay attempt safely refuses (unknown algorithm) rather
       than guessing. */
    struct BootloaderEntry { uint8_t version[4]; uint8_t algorithm; };
    enum { BOOTLOADER_TABLE_MAX = 16 };
    BootloaderEntry bootloaderTable[BOOTLOADER_TABLE_MAX];
    uint8_t bootloaderTableCount;
    uint8_t lookupBootloaderAlgorithm(const uint8_t* version);

    /* Auto-registration status, broadcast to the panel over CAN (PGN60 sub-
       packet 4 — see canBroadcast() in work.cpp) since the panel has no MQTT
       access of its own to watch ota-style status topics. Panel shows this
       while waiting for connectionLink to arrive (see startAutoRegister()). */
    enum AutoRegisterStatus { AUTOREG_IDLE, AUTOREG_BUSY, AUTOREG_DONE, AUTOREG_ERROR };
    AutoRegisterStatus autoRegisterStatus;
    void startAutoRegister(void);  /* called from Timberline's CAN dispatch */

    /* Generates a fresh "getlink" token, publishes it retained, and builds
       the resulting https://.../go/<user>/<token> URL directly into
       internet.connectionLink (so dataActualizator.handler() picks up the
       change and pushes it to flash + the panel over CAN, same as any other
       tracked field) — the shared core of both the "getlink" SMS command
       and a just-finished auto-registration. Requires mqtt.username/broker
       already set. */
    void publishLinkToken(void);

    /* Called when "<mqtt.username>/cmd/desired/<name>" arrives (name = last path segment) */
    void (*onMqttCommand)(const char* name, const char* payload);

    /* Access control / general config — persisted in flash */
    struct Config {
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
                                       internet/MQTT while network.isRoaming; true = allowed.
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
    } config;

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
        ANS_HTTPREAD = 1<<18, /* +HTTPREAD: <len> — any chunk header seen, len>0 or 0 */
        ANS_HTTPREAD_DONE = 1<<19, /* +HTTPREAD: 0 — the terminator; the whole (possibly
                                       multi-chunk) read is actually complete now, see
                                       RawCapture's comment in Modem.h and doOta() */
        ANS_CEREG   = 1<<20,  /* +CEREG: — epsRegistered/epsRoaming updated (EPS/LTE
                                  registration; see the comment above csRegistered) */
    };
    uint32_t answer;

    /* +CREG (CS domain: GSM/UTRAN) and +CEREG (PS/EPS domain: E-UTRAN/LTE) are
       tracked separately and OR'd into network.isRegistered/isRoaming — a
       data-only LTE SIM (common with Chinese IoT carriers) never satisfies
       CREG at all (no CS domain to register in), so relying on CREG alone
       left network.isRegistered permanently false despite CGATT/CGACT/HTTP
       all working. Each domain's flags persist across polls of the *other*
       domain (see parseLine()'s +CREG:/+CEREG: handlers), so a stale/never-
       queried domain can't mask a working one. */
    bool csRegistered, csRoaming, epsRegistered, epsRoaming;

    /* ── Capture mode for multi-line responses ───────────────────────── */
    enum CaptureMode { CAP_NONE, CAP_IMEI, CAP_CMGR_BODY, CAP_MQTT_TOPIC, CAP_MQTT_PAYLOAD };
    CaptureMode capture;

    /* ── State machine ───────────────────────────────────────────────── */
    enum ModemState {
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
        ST_FETCH_PROFILE,
        ST_OTA,
        ST_AUTO_REGISTER,
    };
    ModemState state;
    int8_t     step;

    void setState(ModemState s) { state = s; step = 0; answer = 0; capture = CAP_NONE; }

    /* ── Outgoing + incoming SMS scratch ─────────────────────────────── */
    enum { SMS_QUEUE_MAX = 4 };
    struct SmsQueueEntry { char phone[16]; char text[141]; bool pending; };
    struct SmsIo {
        bool pending;   /* true while smsPhone/smsText is the one actively being sent */

        /* Overflow queue for sendSms() calls that arrive while pending is
           already true — e.g. a combined SMS ("server X,login Y,password
           Z,getlink") dispatches multiple command handlers in one synchronous
           pass, each calling sendSms() with its own reply. Without this, every
           call after the first used to be silently dropped (pending was
           already true), so only the first command's confirmation ever went
           out. doIdle() promotes the oldest queued entry into
           smsPhone/smsText once the active send finishes. */
        SmsQueueEntry queue[SMS_QUEUE_MAX];

        /* The slot doReadSms() is actively working on right now — set exactly
           once, by doIdle(), the moment it starts a read (copied from
           notifySlot below). doReadSms()'s own command-building re-reads
           this on every call while its step is in flight; it must NOT change
           out from under an in-progress read, or the AT+CMGR=<slot> command
           string changes mid-wait, atCmd() sees a new hash, and abandons the
           response it was already waiting for (confirmed on real hardware: a
           second +CMTI: arriving while CMGR=1 was still in flight caused the
           firmware to fire CMGR=2 immediately, and CMGR=1's late response then
           landed as a stray, out-of-place +CMGR: line later on, eventually
           producing a bogus CMGR=0 / "CMS ERROR: Invalid memory index"). */
        uint8_t slot;
        /* Slot most recently announced by "+CMTI:"/found by ST_POLL_SMS_UNREAD —
           safe to overwrite any time, from anywhere, since nothing reads it
           while a read is in flight; only doIdle() copies it into slot. */
        uint8_t notifySlot;
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
        bool    notifyPending;
    } sms;

    /* ── USSD ────────────────────────────────────────────────────────── */
    bool     ussdPending;
    char     ussdReq[32];

    /* ── Polling timers ──────────────────────────────────────────────── */
    struct Timers {
        uint32_t csq;
        uint32_t creg;
        uint32_t net;
        uint32_t mqttRetry;
        uint32_t smsPoll; /* fallback safety net — see ST_POLL_SMS_UNREAD */
    } timers;

    /* ── Internet check / HTTP scratch ───────────────────────────────── */
    struct HttpScratch {
        uint16_t status;
        uint16_t dataLen;  /* +HTTPACTION:'s 3rd field — only OTA (doOta())
                               currently reads this; doCheckInternet() doesn't
                               need it, just parsed unconditionally alongside
                               status since it's free. */
    } http;

    /* ── Raw byte capture (bypasses line-oriented parsing entirely) ─────
       Used for AT+HTTPREAD's binary payload, which can contain any byte
       value including '\r'/'\n' — the normal drainRx()/parseLine() line
       splitter would corrupt it.

       Confirmed on real hardware: AT+HTTPREAD's "OK" arrives immediately
       (command *accepted*, same as AT+HTTPACTION) — the actual payload
       shows up later, asynchronously, as one or more "+HTTPREAD: <len>"
       blocks (observed capped around 1024 bytes per block for a 2048-byte
       request), terminated by a final "+HTTPREAD: 0" with no data after
       it. So a single AT+HTTPREAD can involve *multiple* raw chunks into
       the same destination buffer, not one — got tracks the running total
       across all of them; chunkRemaining is what's left of the chunk
       currently being copied (0 = between chunks, waiting for the next
       "+HTTPREAD: <len>" header line or the terminator). A step arms this
       by pointing dst/cap at its destination and zeroing got before issuing
       AT+HTTPREAD, then must poll for ANS_HTTPREAD_DONE (not just atCmd()
       returning true on that first OK) — see doOta(). */
    struct RawCapture {
        uint8_t*  dst;
        uint16_t  cap;  /* dst's actual buffer size — parseLine() clamps a chunk's
                            reported length to what's left of this so a module-
                            reported length bigger than expected (bug/corrupt
                            response) can't overflow the destination */
        uint16_t  got;
        uint16_t  chunkRemaining;
    } rawCapture;

    /* ── MQTT scratch ─────────────────────────────────────────────────── */
    /* A name, once assigned a slot by mqttPublish(), keeps that slot for
       the device's entire runtime (matched by name, never freed) — so this
       must exceed the total number of *distinct* topic names the firmware
       will ever publish, not just how many can be dirty at once (though
       those happen to coincide today: justConnected in Timberline.cpp
       forces every mqttActualizerHandler field dirty on one pass, which is
       the same worst case). Currently ~50: mqttActualizerHandler's ~47
       (control mirrors + zn<N>/... + errors) + "telemetry" + "linkToken" +
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
    struct MqttScratch {
        uint8_t  urcResult;         /* result code from +CMQTTCONNECT:/+CMQTTSUB:/+CMQTTPUB: */
        char     rxName[16];        /* last path segment of an incoming desired-topic        */
        char     rxPayload[32];     /* was [8] — big enough for old on/off/small-number payloads,
                                        but truncated cmd/desired/otaStart's version string (e.g.
                                        "125.0.0.15", 10 chars) down to "125.0.0", silently building
                                        a wrong firmware URL. Sized to match otaScratch.version[24]
                                        (+ headroom) since that's the longest payload this now
                                        needs to hold. */
        bool     teardownThenNet;   /* ST_MQTT_TEARDOWN's next hop: ST_NET_TEARDOWN (true) or
                                        back to ST_IDLE to retry MQTT alone (false)             */
        bool     netTeardownThenReinit; /* ST_NET_TEARDOWN's next hop: ST_INIT_NET (true, so a live
                                        force2gOnly change actually re-issues AT+CNMP with the
                                        new value) or back to ST_IDLE as usual (false)           */
        bool     reconnectRequested; /* set by mqttForceReconnect(), consumed by doIdle() —
                                         never setState() directly there: mqttForceReconnect()
                                         can be called re-entrantly from inside onSmsReceived,
                                         itself invoked from doReadSms(), and clobbering
                                         state/step out from under the caller's own state
                                         handler would corrupt the state machine.            */
        MqttPubEntry pubQueue[MQTT_PUB_MAX];
    } mqttScratch;
    bool  mqttQueueHasPending(void);

    /* ── OTA scratch (see doOta(), startOta()) ───────────────────────────
       chunkBuf/verifyBuf/pageCrc's sizes below (2048, 2048, 64) hardcode
       flash.h's FLASH_PAGE_SIZE/FLASH_OTA_PAGE_COUNT rather than including
       flash.h here (keeps it out of Modem.h's public include surface) —
       Modem.cpp static_asserts the two stay in sync. RAM cost (~4 KB) is a
       non-issue given ~114 KB is free (see the flash/RAM budget discussion
       this feature was scoped against). */
    struct OtaScratch {
        bool     startRequested;   /* set by startOta(), consumed by doIdle() —
                                       same reentrancy rationale as
                                       mqttScratch.reconnectRequested above:
                                       startOta() can be called from
                                       onMqttCommandReceived(), itself invoked
                                       mid-parseLine(), so it must not setState()
                                       directly out from under whatever state
                                       handler is currently running. */
        uint8_t  deviceType;        /* requested target type — copied into ota.deviceType
                                        once doFetchProfile() actually confirms a profile
                                        exists for it, see ST_FETCH_PROFILE */
        char     version[24];
        uint32_t pageCrc[MODEM_OTA_PAGE_COUNT]; /* target CRC32 per page, from firmware.crc32 */
        uint8_t  retries;
        bool     failed;
        uint8_t  chunkBuf[MODEM_OTA_PAGE_SIZE];  /* raw bytes just downloaded for the current page */
        uint8_t  verifyBuf[MODEM_OTA_PAGE_SIZE]; /* read back from flash after writing, for memcmp */
        uint16_t readLen; /* requested AT+HTTPREAD length, stashed by the "send" step for the
                              matching "wait" step to check rawCapture.got against — http.dataLen
                              can't be used for this, since every "+HTTPREAD: <n>" chunk header,
                              including the final "+HTTPREAD: 0" terminator, overwrites it. */
    } otaScratch;
    void     doOta(void);
    void     doFetchProfile(void);  /* runs before doOta() — see ST_FETCH_PROFILE; also fetches
                                        bootloaders.txt into bootloaderTable[], see its comment */
    void     parseBootloaderTable(const char* buf);  /* fills bootloaderTable[]/bootloaderTableCount */
    void     refreshStagedInfo(void);  /* re-reads ota.stagedValid/stagedVersion/stagedBytes from flash */

    /* ── Auto-registration scratch (see doAutoRegister(), startAutoRegister()) ── */
    struct RegScratch {
        bool    startRequested;  /* set by startAutoRegister(), consumed by doIdle() —
                                     same reentrancy rationale as otaScratch.startRequested
                                     above: the CAN dispatch that calls startAutoRegister()
                                     must not setState() directly out from under whatever
                                     state handler is currently running. */
        char    login[16];       /* candidate login, regenerated on each retry after a 409 */
        char    password[24];    /* generated once per attempt, kept across login retries */
        uint8_t retries;
    } regScratch;
    void     doAutoRegister(void);

    /* ── RX line accumulator ─────────────────────────────────────────── */
    /* 256 wasn't enough for a UCS2-hex-encoded SMS body (see
       decodeUcs2Hex()): a single-segment UCS2 SMS carries up to 70
       characters, hex-encoded as 4 digits each = 280 hex digits on that one
       AT+CMGR body line alone — longer than the old 255 usable bytes
       (LINE_SIZE-1, see the "c != '\r' && rx.len < LINE_SIZE-1" guard where
       this gets filled). The line silently truncated mid-hex-digit, so
       rx.len%4 no longer lined up with a whole number of UCS2 code units,
       decodeUcs2Hex()'s all-hex/length%4==0 check correctly refused to
       treat it as UCS2, and the raw (truncated) hex string got displayed
       verbatim instead of decoded Cyrillic — confirmed on real hardware,
       2026-08-24. 320 comfortably covers the 280-digit worst case with
       margin. */
    static const uint16_t LINE_SIZE = 320;
    struct RxLine {
        uint16_t cursor;
        char     buf[LINE_SIZE];
        uint16_t len;
    } rx;

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
