#include "work.h"
#include "Version.h"
#include "Modem.h"
#include "Timberline.h"
#include "CanRelay.h"
#include "FaultManager.h"
#include "DataActualizator.h"
#include "StringTransfer.h"
#include "modem_handler.h"
#include "can.h"
#include "core.h"
#include "flash.h"
#include "log.h"
#include "ntc.h"
#include "button.h"
#include "randomize.h"

#include <string.h>

Work_C work;

Work_C::Work_C(void) {}

void Work_C::initialize(void) {
    timberline.init();
}

void Work_C::handler(void) {
    resetHandler();
    modem_process_emulated_sms();
    modem_process_usb_set();
    faultManager.handler();
    dataActualizator.handler();
    timberline.mqttActualizerHandler();
    timberline.mqttTelemetryHandler();
    timberline.timeSyncHandler();
    timberline.tempAlarmHandler();
    timberline.statsHandler();
    timberline.expireStaleDevices();
    canRelay.handler();

    /* canBroadcast() (periodic PGN18/60 + the string round-robin it drives)
       and stringTransfer.handler() (paces any string transfer in progress,
       PGN61/62) both compete for the same 3 CAN TX mailboxes as
       canRelay.handler()'s PGN=106 fragment stream — confirmed on real
       hardware that even with mailbox checks and inter-frame pacing,
       occasional frames still got dropped, most likely lost arbitration/
       mailbox contention against this other routine traffic. Neither is
       time-critical enough to matter losing a few seconds of updates
       during the one-off, already-slow (tens of seconds) firmware relay,
       so just don't compete with it. */
    if (canRelay.status != CanRelay::RELAY_STAGING) {
        canBroadcast();
        stringTransfer.handler();
    }
}

/* ── canBroadcast ─────────────────────────────────────────────────────────
 * PGN 18 — version/presence announcement (every 5 s)
 * PGN 60 — GSM status, multi-packet: D[0] selects the sub-packet:
 *   0 — registration/roaming + internet + CSQ — sent on change (see below),
 *       plus resent every 5 s in case a panel missed the change-triggered
 *       one, same pattern as sub-packet 1
 *   1 — settings flags — sent on change by DataActualizator, plus resent
 *       here every 10 s in case a panel missed the change-triggered one
 *   2 — operator code: MCC/MNC as two big-endian Uint16 plus an MNC
 *       digit-count byte (2 or 3) — always sent, even once the operator
 *       resolves to a name (pushed separately via STRID_OPERATOR_NAME,
 *       PGN61/62, when that happens) — shown together on the panel so the
 *       resolved name can be cross-checked against the raw code
 *   3 — LAC + Cell ID
 *   4 — auto-registration status (change-triggered)
 *   5 — button (D[1]) and bootloader version (D[2..5]); 0xFF if the
 *       bootloader has no record at 0x080097F0
 *   6 — serial number, big-endian uint32 in D[1..4] (0xFFFFFFFF if blank);
 *       assembly number, big-endian uint16 in D[5..6] (0xFFFF if blank).
 *       A frame with this layout from any sender except another modem
 *       programs a field that is still blank. 0xFF in a field leaves it.
 * SIMCOM revision is not a PGN 60 sub-packet. It is STRID_SIMCOM_FW (20),
 * pushed with the other strings (PGN 61/62) and answered by a PGN 61 request.
 * Sub-packets 2, 3 and 6 are sent every 10 s.
 * Air temperature is D[4] of sub-packet 0. GSM signal is D[2] of
 * sub-packet 0. The operator (GPRS) code is sub-packet 2.
 * Processor temperature is not a PGN 60 byte: it is PGN 13 D[2]
 * (celsius+75), the same encoding a heater already uses.                  */
/* Internal die temperature from the ADC channel Randomize already samples.
   Same +75 wire encoding as heater PGN 13 D[2]. 0xFF until the ADC has a
   plausible reading. */
static uint8_t boardTempByte(void) {
    uint32_t sum = 0;
    for (int i = 0; i < Randomize::ARRAY_SIZE; i++) sum += randomize.array[i];
    uint32_t raw = sum / Randomize::ARRAY_SIZE;
    if (raw < 200 || raw > 3900) return 0xFF;
    int mv = (int)(raw * 3300u / 4095u);
    int t = (1430 - mv) * 10 / 43 + 25;
    if (t < -40 || t > 125) return 0xFF;
    return (uint8_t)(t + 75);
}

/* nations-bootloader plants this at 0x080097F0. An older bootloader has no
   record, and the four version bytes stay 0xFF. */
static void readBootloaderVersion(uint8_t out[4]) {
    const uint32_t magic = *(__IO uint32_t*)0x080097F0u;
    if (magic != 0xB00710ADu) {
        out[0] = out[1] = out[2] = out[3] = 0xFF;
        return;
    }
    for (int i = 0; i < 4; i++) out[i] = *(__IO uint8_t*)(0x080097F4u + i);
}

/* PGN 6 is handled in the CAN RX interrupt, which must not burst
   SendMessage(): three mailboxes, and the call drops whatever does not fit.
   The interrupt only queues. canBroadcast() drains one frame per free mailbox. */
enum { REPLY_Q = 16 };
struct ReplyFrame { uint32_t id; uint8_t d[8]; };
static ReplyFrame replyQ[REPLY_Q];
static volatile uint8_t replyHead = 0;
static volatile uint8_t replyTail = 0;

static void enqueueReply(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                         uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
    uint8_t next = (uint8_t)((replyTail + 1) % REPLY_Q);
    if (next == replyHead) return;
    ReplyFrame* f = &replyQ[replyTail];
    f->id = id;
    f->d[0] = d0; f->d[1] = d1; f->d[2] = d2; f->d[3] = d3;
    f->d[4] = d4; f->d[5] = d5; f->d[6] = d6; f->d[7] = d7;
    replyTail = next;
}

static void pumpReplyQueue(void) {
    if (replyHead == replyTail || !can.txReady()) return;
    ReplyFrame* f = &replyQ[replyHead];
    can.SendMessage(f->id, f->d[0], f->d[1], f->d[2], f->d[3], f->d[4], f->d[5], f->d[6], f->d[7]);
    replyHead = (uint8_t)((replyHead + 1) % REPLY_Q);
}

static uint32_t directedId(uint16_t pgn, uint8_t toType, uint8_t toAddr) {
    return ((uint32_t)pgn << 20) | ((uint32_t)toType << 13) | ((uint32_t)toAddr << 10)
         | ((uint32_t)can.idType << 3) | can.idAddress;
}

static void enqueuePgn60(uint8_t sub, uint8_t toType, uint8_t toAddr) {
    uint32_t id = directedId(60, toType, toAddr);
    switch (sub) {
    case 0: {
        uint8_t d1 = (uint8_t)(  (modem.network.isRegistered ? 1u : 0u)
                                | ((modem.network.isRoaming ? 1u : 0u) << 2)
                                | ((modem.internet.isInternetConnected ? 1u : 0u) << 4)
                                | ((modem.mqtt.connected ? 1u : 0u) << 6));
        enqueueReply(id, 0, d1, modem.network.csq, modem.network.networkAcT,
            ntc.connected ? (uint8_t)(ntc.temperature + 75) : 0xFF, 0xFF, 0xFF, 0xFF);
        break;
    }
    case 1: {
        /* Same bytes as DataActualizator::sendSettings(). onlySms is the
           inverted useInternet flag, the polarity already on the wire. */
        uint8_t d1 = (uint8_t)(  (!modem.config.useInternet ? 1u : 0u)
                                | ((modem.config.faultReport ? 1u : 0u) << 2)
                                | ((modem.config.cmdAck ? 1u : 0u) << 4)
                                | ((uint32_t)(modem.config.tempUnit & 1) << 6));
        enqueueReply(id, 1, d1, modem.config.force2gOnly ? 1 : 0,
            modem.config.allowRoaming ? 1 : 0, 0xFF, 0xFF, 0xFF, 0xFF);
        break;
    }
    case 2: {
        uint16_t mcc = 0, mnc = 0;
        uint8_t mncDigits = 0;
        const char* p = modem.network.operatorCode;
        uint8_t i = 0;
        for (; i < 3 && p[i]; i++) mcc = (uint16_t)(mcc * 10 + (uint16_t)(p[i] - '0'));
        for (; p[i]; i++) { mnc = (uint16_t)(mnc * 10 + (uint16_t)(p[i] - '0')); mncDigits++; }
        enqueueReply(id, 2, (uint8_t)(mcc >> 8), (uint8_t)mcc, (uint8_t)(mnc >> 8), (uint8_t)mnc,
            mncDigits, 0xFF, 0xFF);
        break;
    }
    case 3:
        enqueueReply(id, 3,
            (uint8_t)(modem.network.lac >> 8), (uint8_t)modem.network.lac,
            (uint8_t)(modem.network.cellId >> 24), (uint8_t)(modem.network.cellId >> 16),
            (uint8_t)(modem.network.cellId >> 8), (uint8_t)modem.network.cellId, 0xFF);
        break;
    case 4:
        enqueueReply(id, 4, (uint8_t)modem.autoRegisterStatus, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
        break;
    case 5: {
        uint8_t bl[4];
        readBootloaderVersion(bl);
        enqueueReply(id, 5, button.debounced ? 1u : 0u, bl[0], bl[1], bl[2], bl[3], 0xFF, 0xFF);
        break;
    }
    case 6: {
        uint32_t serial = flash.serialNumber();
        uint16_t assembly = flash.assemblyNumber();
        enqueueReply(id, 6,
            (uint8_t)(serial >> 24), (uint8_t)(serial >> 16),
            (uint8_t)(serial >> 8), (uint8_t)serial,
            (uint8_t)(assembly >> 8), (uint8_t)assembly, 0xFF);
        break;
    }
    case 8: {
        uint32_t puk = flash.pukNumber();
        enqueueReply(id, 8,
            (uint8_t)(puk >> 24), (uint8_t)(puk >> 16),
            (uint8_t)(puk >> 8), (uint8_t)puk, 0xFF, 0xFF, 0xFF);
        break;
    }
    default:
        break;
    }
}

void replyToPgnRequest(uint16_t pgn, uint8_t sub, uint8_t toType, uint8_t toAddr) {
    if (pgn == 13) {
        enqueueReply(directedId(13, toType, toAddr),
            0xFF, 0xFF, boardTempByte(), 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
        return;
    }
    if (pgn == 18) {
        enqueueReply(directedId(18, toType, toAddr),
            VERSION_1, VERSION_2, VERSION_3, VERSION_4, 0xFF, 0xFF, 0xFF, 0xFF);
        return;
    }
    if (pgn != 60) return;
    /* 0xFF asks for every periodic sub-packet. PUK (8) stays opt-in. */
    if (sub == 0xFF) {
        static const uint8_t all[] = { 0, 1, 2, 3, 4, 5, 6 };
        for (uint8_t i = 0; i < sizeof(all); i++) enqueuePgn60(all[i], toType, toAddr);
        return;
    }
    enqueuePgn60(sub, toType, toAddr);
}

void Work_C::canBroadcast(void) {
    pumpReplyQueue();

    static uint32_t timer     = 0;
    static uint32_t timerSlow = 0;
    static uint8_t  prevD1    = 0xFFu; /* forces an immediate first send once real state exists */
    /* Can::BROADCAST_TYPE/BROADCAST_ADDRESS, same convention as id1 below - this used to be
       (can.idType, can.idAddress) (itself), so every PGN60 frame was addressed back to the
       modem instead of the panel. PU28-Timberline's own ProcessMessage() doesn't actually check
       the destination for PGN60 (see its messages.cpp comment), so the panel picked these up
       anyway - but any receiver that DOES check the destination would ignore them. */
    uint32_t id60 = (60u<<20) | ((uint32_t)Can::BROADCAST_TYPE<<13) | ((uint32_t)Can::BROADCAST_ADDRESS<<10)
                  | ((uint32_t)can.idType<<3) | can.idAddress;

    /* Sub-packet 0: D[1] = 2 bits/bool (00=off,01=on,11=no data):
     *   bits0-1 registered, bits2-3 roaming, bits4-5 internet connected
     *   (only meaningful when useInternet), bits6-7 MQTT connected
     *   (only meaningful when useInternet && isInternetConnected). D[2]=CSQ.
     *   D[3] = networkAcT, raw <AcT> from the last +COPS? poll (real
     *   network tech — the panel buckets it into 2G/3G/4G for display).
     *   D[4] = optional external NTC on A1 (see Library/Ntc), same
     *   value+75 offset as every other temperature on this protocol
     *   (floorTemperature/engineTemperature etc. — see Timberline.cpp),
     *   0xFF when ntc.connected is false (sensor not wired up).
     *   Recomputed every tick (cheap — a handful of bitfield reads) and
     *   compared against the last sent value so a genuine transition
     *   (just registered, internet came up, MQTT connected/dropped) reaches
     *   the panel right away — confirmed on real hardware that relying on
     *   the fixed periodic send alone left the panel's modem-status icon
     *   showing a stale colour for up to ~5s after the real change. CSQ/AcT/
     *   modem temp changing on their own don't trigger a resend — they're
     *   covered by the periodic tick below, and diffing them here would spam
     *   a resend on every signal-strength wobble even though nothing
     *   meaningful (registration/internet/mqtt) actually changed. */
    uint8_t d1 = (uint8_t)(  (modem.network.isRegistered ? 1u : 0u)
                            | ((modem.network.isRoaming   ? 1u : 0u) << 2)
                            | ((modem.internet.isInternetConnected ? 1u : 0u) << 4)
                            | ((modem.mqtt.connected ? 1u : 0u) << 6));
    bool dueForPeriodic = (core.getTick() - timer) >= 5000;
    if (d1 != prevD1 || dueForPeriodic) {
        prevD1 = d1;
        timer = core.getTick();
        can.SendMessage(id60,
            0, d1, modem.network.csq, modem.network.networkAcT,
            ntc.connected ? (uint8_t)(ntc.temperature + 75) : 0xFF,
            0xFF, 0xFF, 0xFF);
        /* PGN 13, same layout as a heater: D[2] is the processor
           temperature (celsius+75). The modem has no flame, liquid or
           overheat sensors, so those bytes stay 0xFF. */
        uint32_t id13 = (13u<<20) | ((uint32_t)Can::BROADCAST_TYPE<<13) | ((uint32_t)Can::BROADCAST_ADDRESS<<10)
                      | ((uint32_t)can.idType<<3) | can.idAddress;
        can.SendMessage(id13, 0xFF, 0xFF, boardTempByte(), 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    /* Sub-packet 4: auto-registration status (see doAutoRegister() in
     * Modem.cpp) — change-triggered, not tied to the 5s/10s periodic timers
     * above, since the whole point is the panel seeing "busy" -> "done"/
     * "error" within the few seconds the HTTP round-trip actually takes, not
     * up to 10s late. idle<->busy<->done/error are the only transitions that
     * matter here; no periodic safety-net resend needed since this isn't a
     * persistent setting like sub-packets 0-3, just a one-shot status the
     * panel is actively watching while the button's own screen is open. */
    static uint8_t prevRegStatus = 0xFFu;
    if ((uint8_t)modem.autoRegisterStatus != prevRegStatus) {
        prevRegStatus = (uint8_t)modem.autoRegisterStatus;
        can.SendMessage(id60,
            4, prevRegStatus, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    if (dueForPeriodic) {
        /* Same self-addressing bug as id60 above - broadcast to everyone, not to self. */
        uint32_t id18 = (18u<<20) | ((uint32_t)Can::BROADCAST_TYPE<<13) | ((uint32_t)Can::BROADCAST_ADDRESS<<10)
                      | ((uint32_t)can.idType<<3) | can.idAddress;
        can.SendMessage(id18,
            VERSION_1, VERSION_2, VERSION_3, VERSION_4,
            0xFF, 0xFF, 0xFF, 0xFF);
    }

    /* PGN=1 [0,0] broadcast (Can::BROADCAST_TYPE/BROADCAST_ADDRESS - the all-bits-set
       wildcard address, same convention PU28-Timberline's own messages.cpp
       uses for its own broadcast queries) — "who are you". Now just a
       last-resort fallback: Timberline::maybeQueryNewDevice() already
       targets a PGN=6 [0,18] query at any newly-seen sender the moment it
       first speaks (see ProcessCanMessage()), which every current device
       type answers, PU-28's panel firmware included (see its own case 6
       handler in messages.cpp) — so this broadcast only still matters for
       a hypothetical device that speaks neither PGN=18 unprompted nor
       PGN=6. Kept, but well below the 5s telemetry cadence — a minute's
       discovery latency for that edge case is a non-issue, and this way it
       isn't adding to the bus's regular 5s traffic. */
    static uint32_t timerDiscoveryBroadcast = 0;
    if ((core.getTick() - timerDiscoveryBroadcast) >= 60000) {
        timerDiscoveryBroadcast = core.getTick();
        uint32_t id1 = (1u<<20) | ((uint32_t)Can::BROADCAST_TYPE<<13) | ((uint32_t)Can::BROADCAST_ADDRESS<<10)
                     | ((uint32_t)can.idType<<3) | can.idAddress;
        can.SendMessage(id1, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    if ((core.getTick() - timerSlow) >= 10000) {
        timerSlow = core.getTick();

        /* Sub-packet 1: settings flags — periodic safety-net resend. */
        dataActualizator.resendSettings();

        /* Operator NAME (PGN61/62 string) — sent whenever it's resolved and
           has changed since the last send. Independent of sub-packet 2
           below now (used to be an if/else: the code only went out while
           the name was unresolved — see that block's own comment for why
           that changed). */
        static char lastOperatorName[24] = {0};
        if (modem.network.operatorName[0]) {
            if (strcmp(lastOperatorName, modem.network.operatorName) != 0) {
                strncpy(lastOperatorName, modem.network.operatorName, sizeof(lastOperatorName)-1);
                lastOperatorName[sizeof(lastOperatorName)-1] = 0;
                /* Broadcast, not to self - same bug as id60/id18 above. */
                stringTransfer.sendString(modem.network.operatorName, STRID_OPERATOR_NAME,
                                           Can::BROADCAST_TYPE, Can::BROADCAST_ADDRESS);
            }
        } else {
            lastOperatorName[0] = 0;
        }

        /* Sub-packet 2: operator code as MCC/MNC, two big-endian Uint16 plus
           an MNC digit-count byte — binary instead of the previous 5 raw
           ASCII digits (didn't match this protocol's own conventions —
           every other multi-byte field here, LAC/CellID right below
           included, is binary). Parsed from network.operatorCode (the
           plain digit string AT+COPS reports, e.g. "25001") rather than
           replacing that string's own storage — it's still used as-is for
           the operator_names.cpp lookup and the Deutsche-Telekom-APN
           special case (see Modem.cpp).

           D[5] = MNC digit count (2 or 3 — 3GPP allows both; derived here
           from how many characters are actually left in the string after
           MCC's fixed 3 digits, exactly what AT+COPS itself reported).
           Genuinely needed, not cosmetic: MNC "01" (2 digits) and a
           hypothetical 3-digit MNC "001" are different real PLMN codes
           that both parse to the numeric value 1 — a receiver formatting
           this back into the human-recognizable 5/6-digit code (e.g.
           Beeline RU = MCC 250 + MNC 01 = "25001", not "2501") needs the
           original width to zero-pad correctly, not just the value.

           Sent unconditionally on this same 10s tick now (was: only while
           operatorName was empty) — there's real diagnostic value either
           way (lets the code be cross-checked against the resolved name
           right on the panel screen, see ModemInfo.cpp's DrawOperator()),
           and it's only 5 bytes now instead of an ASCII string, so no
           bandwidth reason left to suppress it once a name is known. 0/0/0
           while operatorCode itself is still empty (not yet queried) — no
           separate "unknown" sentinel, same as LAC/CellID below; no real
           MCC is ever 000. */
        uint16_t mcc = 0, mnc = 0;
        uint8_t  mncDigits = 0;
        {
            const char* p = modem.network.operatorCode;
            uint8_t i = 0;
            for (; i < 3 && p[i]; i++) mcc = (uint16_t)(mcc * 10 + (uint16_t)(p[i] - '0'));
            for (; p[i]; i++) { mnc = (uint16_t)(mnc * 10 + (uint16_t)(p[i] - '0')); mncDigits++; }
        }
        can.SendMessage(id60,
            2, (uint8_t)(mcc>>8), (uint8_t)mcc, (uint8_t)(mnc>>8), (uint8_t)mnc, mncDigits, 0xFF, 0xFF);

        /* Sub-packet 3: LAC (16-bit) + Cell ID (32-bit), big-endian */
        can.SendMessage(id60,
            3,
            (uint8_t)(modem.network.lac>>8),    (uint8_t)modem.network.lac,
            (uint8_t)(modem.network.cellId>>24),(uint8_t)(modem.network.cellId>>16),
            (uint8_t)(modem.network.cellId>>8), (uint8_t)modem.network.cellId,
            0xFF);

        uint32_t serial = flash.serialNumber();
        uint16_t assembly = flash.assemblyNumber();
        can.SendMessage(id60,
            6,
            (uint8_t)(serial >> 24), (uint8_t)(serial >> 16),
            (uint8_t)(serial >> 8),  (uint8_t)serial,
            (uint8_t)(assembly >> 8), (uint8_t)assembly, 0xFF);
    }

    /* Sub-packet 5: button and bootloader version. A press goes out
       immediately; otherwise once a second so a missed frame is recovered. */
    static uint8_t  prevBtn = 0xFFu;
    static uint32_t timerStand = 0;
    uint8_t btn = button.debounced ? 1u : 0u;
    if (btn != prevBtn || (core.getTick() - timerStand) >= 1000) {
        prevBtn = btn;
        timerStand = core.getTick();
        uint8_t bl[4];
        readBootloaderVersion(bl);
        can.SendMessage(id60, 5, btn, bl[0], bl[1], bl[2], bl[3], 0xFF, 0xFF);
    }

    /* Round-robin push of every registered string (IMEI, PIN, phones, SMS
       text/numbers, operator name/code, IP, mqtt/broker/login/password,
       internetCheckUrl, connectionLink, ...), one per tick, cycling back to
       the start once all are sent — including empty ones. On-demand
       request/response alone proved unreliable in practice (a missed
       request or a dropped mid-transfer packet just left a field stale
       until the user reloaded the screen); this guarantees every field is
       refreshed within one full cycle regardless. DataActualizator's own
       sendString() on real changes (see DataActualizator.cpp) still fires
       immediately on top of this for the "don't wait for the cycle" case. */
    static uint32_t timerStr = 0;
    if ((core.getTick() - timerStr) >= 2000) {
        timerStr = core.getTick();
        stringTransfer.broadcastNext(Can::BROADCAST_TYPE, Can::BROADCAST_ADDRESS); /* not to self - same bug as above */
    }
}

void Work_C::resetHandler(void) {
    static uint32_t timerReset  = 0;
    static uint32_t timerTick   = 0;

    /* Increment linkCnt every second (CAN ISR resets it to 0 on each RX) */
    if ((core.getTick() - timerTick) >= 1000) {
        timerTick = core.getTick();
        can.linkCnt++;
    }

    /* Kick watchdog while CAN is alive (message in last ~3 s) */
    if (can.linkCnt < 3)
        timerReset = core.getTick();

    if ((core.getTick() - timerReset) > (15 * 60 * 1000)) {
        /* No flash.writeSetup() here on purpose: every setting change already
           persists synchronously at the point it's made (admin/phone/setpin/
           unit/faultreport/ack — see Timberline.cpp), so there's nothing
           pending to flush. Writing here would just wear the flash on every
           routine CAN-silence reset, and if the silence is itself a symptom
           of something wrong, it'd risk baking corrupted RAM state into
           persistent storage right before rebooting. */
        *(__IO uint32_t *)(0x20023F00) = 0x00000000;
        NVIC_SystemReset();
    }
}
