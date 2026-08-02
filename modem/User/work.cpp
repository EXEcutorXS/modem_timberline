#include "work.h"
#include "Modem.h"
#include "Timberline.h"
#include "FaultManager.h"
#include "DataActualizator.h"
#include "StringTransfer.h"
#include "modem_handler.h"
#include "can.h"
#include "core.h"
#include "flash.h"
#include "log.h"

#include <string.h>

Work_C work;

Work_C::Work_C(void) {}

void Work_C::initialize(void) {
    timberline.init();
}

void Work_C::handler(void) {
    resetHandler();
    modem_process_emulated_sms();
    faultManager.handler();
    dataActualizator.handler();
    timberline.mqttActualizerHandler();
    timberline.mqttTelemetryHandler();
    timberline.doCanRelay();

    /* canBroadcast() (periodic PGN18/60 + the string round-robin it drives)
       and stringTransfer.handler() (paces any string transfer in progress,
       PGN61/62) both compete for the same 3 CAN TX mailboxes as
       doCanRelay()'s PGN=106 fragment stream — confirmed on real hardware
       that even with mailbox checks and inter-frame pacing, occasional
       frames still got dropped, most likely lost arbitration/mailbox
       contention against this other routine traffic. Neither is
       time-critical enough to matter losing a few seconds of updates
       during the one-off, already-slow (tens of seconds) firmware relay,
       so just don't compete with it. */
    if (timberline.canRelay.status != Timberline::RELAY_STAGING) {
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
 *   2 — operator code (numeric MCC+MNC, ASCII digits) — only when the
 *       operator isn't in the operator_names.cpp table; if it is, the
 *       resolved name is pushed instead via STRID_OPERATOR_NAME (PGN61/62)
 *   3 — LAC + Cell ID
 *   Sub-packets 2-3 rarely change, sent every 10 s.
 * IMEI (and other long/variable strings) are no longer packed into PGN60 —
 * they're transferred on demand via the generic PGN61/62 string protocol,
 * see StringTransfer.cpp.                                                  */
void Work_C::canBroadcast(void) {
    static uint32_t timer     = 0;
    static uint32_t timerSlow = 0;
    static uint8_t  prevD1    = 0xFFu; /* forces an immediate first send once real state exists */
    uint32_t id60 = (60u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                  | ((uint32_t)can.idType<<3) | can.idAddress;

    /* Sub-packet 0: D[1] = 2 bits/bool (00=off,01=on,11=no data):
     *   bits0-1 registered, bits2-3 roaming, bits4-5 internet connected
     *   (only meaningful when useInternet), bits6-7 MQTT connected
     *   (only meaningful when useInternet && isInternetConnected). D[2]=CSQ.
     *   D[3] = networkAcT, raw <AcT> from the last +COPS? poll (real
     *   network tech — the panel buckets it into 2G/3G/4G for display).
     *   Recomputed every tick (cheap — a handful of bitfield reads) and
     *   compared against the last sent value so a genuine transition
     *   (just registered, internet came up, MQTT connected/dropped) reaches
     *   the panel right away — confirmed on real hardware that relying on
     *   the fixed periodic send alone left the panel's modem-status icon
     *   showing a stale colour for up to ~5s after the real change. CSQ/AcT
     *   changing on their own doesn't trigger a resend — they're covered by
     *   the periodic tick below, and diffing them here would spam a resend
     *   on every signal-strength wobble even though nothing meaningful
     *   (registration/internet/mqtt) actually changed. */
    uint8_t d1 = (uint8_t)(  (modem.network.isRegistered ? 1u : 0u)
                            | ((modem.network.isRoaming   ? 1u : 0u) << 2)
                            | ((modem.internet.isInternetConnected ? 1u : 0u) << 4)
                            | ((modem.mqtt.connected ? 1u : 0u) << 6));
    bool dueForPeriodic = (core.getTick() - timer) >= 5000;
    if (d1 != prevD1 || dueForPeriodic) {
        prevD1 = d1;
        timer = core.getTick();
        can.SendMessage(id60,
            0, d1, modem.network.csq, modem.network.networkAcT, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    if (dueForPeriodic) {
        uint32_t id18 = (18u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                      | ((uint32_t)can.idType<<3) | can.idAddress;
        can.SendMessage(id18,
            VERSION_1, VERSION_2, VERSION_3, VERSION_4,
            0xFF, 0xFF, 0xFF, 0xFF);
    }

    if ((core.getTick() - timerSlow) >= 10000) {
        timerSlow = core.getTick();

        /* Sub-packet 1: settings flags — periodic safety-net resend. */
        dataActualizator.resendSettings();

        /* Sub-packet 2: operator code, up to 5 ASCII digits (MCC+MNC) —
           only sent when the operator isn't resolved to a name below. */
        static char lastOperatorName[24] = {0};
        if (modem.network.operatorName[0]) {
            if (strcmp(lastOperatorName, modem.network.operatorName) != 0) {
                strncpy(lastOperatorName, modem.network.operatorName, sizeof(lastOperatorName)-1);
                lastOperatorName[sizeof(lastOperatorName)-1] = 0;
                stringTransfer.sendString(modem.network.operatorName, STRID_OPERATOR_NAME,
                                           can.idType, can.idAddress);
            }
        } else {
            lastOperatorName[0] = 0;
            char op[5] = {0xFF,0xFF,0xFF,0xFF,0xFF};
            for (uint8_t i = 0; i < 5 && modem.network.operatorCode[i]; i++)
                op[i] = modem.network.operatorCode[i];
            can.SendMessage(id60,
                2, op[0], op[1], op[2], op[3], op[4], 0xFF, 0xFF);
        }

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
        stringTransfer.broadcastNext(can.idType, can.idAddress);
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
