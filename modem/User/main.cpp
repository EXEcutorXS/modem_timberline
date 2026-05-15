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
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"

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

    Set_System();
    USB_Interrupts_Config();
    Set_USBClock();
    USB_Init();

    work.initialize();

    changeMode(MODE_INIT);

    while (true) {
        core.handler();
        gsm.handler();
        can.handler();
        button.handler();
        led.handler();
        work.handler();
        modemHandler();
    }
}
