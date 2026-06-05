#include "work.h"
#include "Modem.h"
#include "Timberline.h"
#include "FaultManager.h"
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
}

/* ── canBroadcast ─────────────────────────────────────────────────────────
 * PGN 18 — version/presence announcement (every 5 s)
 * PGN 60 — GSM status: registration, signal, settings flags (every 5 s)  */
void Work_C::canBroadcast(void) {
    static uint32_t timer = 0;
    if ((core.getTick() - timer) < 5000) return;
    timer = core.getTick();

    uint32_t id18 = (18u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                  | ((uint32_t)can.idType<<3) | can.idAddress;
    can.SendMessage(id18,
        VERSION_1, VERSION_2, VERSION_3, VERSION_4,
        0xFF, 0xFF, 0xFF, 0xFF);

    /* 2-bit encoding per bool: 0b00=off, 0b01=on, 0b11=no data
       D[0]: bits 0-1=registered, bits 2-3=roaming,
             bits 4-5=faultReport, bits 6-7=cmdAck
       D[1]: bits 0-1=tempUnit (0=C,1=F), bits 2-7=0b111111 (no data)
       D[2]: CSQ 0-31, 0xFF=unknown
       D[3..7]: 0xFF reserved                                              */
    uint8_t d0 = (uint8_t)(  (modem.isRegistered ? 1 : 0)
                           | ((modem.isRoaming    ? 1 : 0) << 2)
                           | ((modem.faultReport  ? 1 : 0) << 4)
                           | ((modem.cmdAck       ? 1 : 0) << 6));
    uint8_t d1 = (uint8_t)(0xFC | (modem.tempUnit & 1));  /* bits 2-7 = 0b111111 */
    uint8_t csq = modem.csq;   /* 0-31 valid, 0xFF = unknown */

    uint32_t id60 = (60u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
                  | ((uint32_t)can.idType<<3) | can.idAddress;
    can.SendMessage(id60,
        d0, d1, csq, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
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
        flash.writeSetup();
        *(__IO uint32_t *)(0x20023F00) = 0x00000000;
        NVIC_SystemReset();
    }
}
