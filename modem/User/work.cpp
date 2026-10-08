#include "work.h"
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
 *   Sub-packets 2-3 rarely change, sent every 10 s.
 * IMEI (and other long/variable strings) are no longer packed into PGN60 —
 * they're transferred on demand via the generic PGN61/62 string protocol,
 * see StringTransfer.cpp.                                                  */
void Work_C::canBroadcast(void) {
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
