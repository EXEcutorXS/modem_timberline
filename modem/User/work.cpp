#include "work.h"
#include "Modem.h"
#include "Timberline.h"
#include "led.h"
#include "button.h"
#include "log.h"
#include "flash.h"
#include "core.h"

#include <string.h>

Work_C work;

Work_C::Work_C(void) {}

void Work_C::initialize(void) {
    timberline.init();
}

void Work_C::handler(void) {
    resetHandler();
}

void Work_C::resetHandler(void) {
    static uint32_t timerReset = 0;
    if ((core.getTick() - timerReset) > (15 * 60 * 1000)) {
        timerReset = core.getTick();
        flash.writeSetup();
        *(__IO uint32_t *)(0x20023F00) = 0x00000000;
        NVIC_SystemReset();
    }
}
