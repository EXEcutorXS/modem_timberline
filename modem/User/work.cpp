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
    canBroadcast();
    dataActualizator.handler();
    stringTransfer.handler();
}

/* ── canBroadcast ─────────────────────────────────────────────────────────
 * PGN 18 — version/presence announcement (every 5 s)
 * PGN 60 — GSM status, multi-packet: D[0] selects the sub-packet:
 *   0 — registration/roaming + CSQ                  (every 5 s, fast-changing)
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
    uint32_t id60 = (60u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                  | ((uint32_t)can.idType<<3) | can.idAddress;

    if ((core.getTick() - timer) >= 5000) {
        timer = core.getTick();

        uint32_t id18 = (18u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                      | ((uint32_t)can.idType<<3) | can.idAddress;
        can.SendMessage(id18,
            VERSION_1, VERSION_2, VERSION_3, VERSION_4,
            0xFF, 0xFF, 0xFF, 0xFF);

        /* Sub-packet 0: D[1] = 2 bits/bool (00=off,01=on,11=no data):
         *   bits0-1 registered, bits2-3 roaming. D[2]=CSQ */
        uint8_t d1 = (uint8_t)(  (modem.isRegistered ? 1u : 0u)
                                | ((modem.isRoaming   ? 1u : 0u) << 2));
        can.SendMessage(id60,
            0, d1, modem.csq, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    if ((core.getTick() - timerSlow) >= 10000) {
        timerSlow = core.getTick();

        /* Sub-packet 1: settings flags — periodic safety-net resend. */
        dataActualizator.resendSettings();

        /* Sub-packet 2: operator code, up to 5 ASCII digits (MCC+MNC) —
           only sent when the operator isn't resolved to a name below. */
        static char lastOperatorName[24] = {0};
        if (modem.operatorName[0]) {
            if (strcmp(lastOperatorName, modem.operatorName) != 0) {
                strncpy(lastOperatorName, modem.operatorName, sizeof(lastOperatorName)-1);
                lastOperatorName[sizeof(lastOperatorName)-1] = 0;
                stringTransfer.sendString(modem.operatorName, STRID_OPERATOR_NAME,
                                           can.idType, can.idAddress);
            }
        } else {
            lastOperatorName[0] = 0;
            char op[5] = {0xFF,0xFF,0xFF,0xFF,0xFF};
            for (uint8_t i = 0; i < 5 && modem.operatorCode[i]; i++)
                op[i] = modem.operatorCode[i];
            can.SendMessage(id60,
                2, op[0], op[1], op[2], op[3], op[4], 0xFF, 0xFF);
        }

        /* Sub-packet 3: LAC (16-bit) + Cell ID (32-bit), big-endian */
        can.SendMessage(id60,
            3,
            (uint8_t)(modem.lac>>8),    (uint8_t)modem.lac,
            (uint8_t)(modem.cellId>>24),(uint8_t)(modem.cellId>>16),
            (uint8_t)(modem.cellId>>8), (uint8_t)modem.cellId,
            0xFF);
    }

    /* Re-push the last received SMS every 30 s, in case a panel joined the
       bus late or missed the on-arrival push in Timberline.cpp. */
    static uint32_t timerSms = 0;
    if (modem.cmgrBody[0] && (core.getTick() - timerSms) >= 30000) {
        timerSms = core.getTick();
        stringTransfer.sendString(modem.cmgrBody,  STRID_LAST_REC_SMS_TEXT, can.idType, can.idAddress);
        stringTransfer.sendString(modem.cmgrPhone, STRID_LAST_REC_SMS_NUM,  can.idType, can.idAddress);
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
