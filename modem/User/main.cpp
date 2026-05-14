#include "main.h"
#include "core.h"
#include "gsm.h"
#include "sms.h"
#include "can.h"
#include "led.h"
#include "button.h"
#include "randomize.h"
#include "flash.h"
#include "work.h"
#include "modem_handler.h"

const uint8_t _CRC[11] __attribute__((at(0x803C000))) =
{
    0x55, 0x55, 0x55, 0x55,
    0x55, 0x55,
    44, 255, 10, 43,
    0x00
};

int main(void)
{
    core.initialize();
    flash.readSetup();
    flash.readSerial();

    gsm.initialize();
    can.initialize();
    led.initialize();
    button.initialize();
    randomize.initialize();

    work.initialize();

    while (true) {
        core.handler();
        gsm.handler();
        button.handler();
        led.handler();
        work.handler();
    }
}
