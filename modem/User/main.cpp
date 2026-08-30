#include "main.h"
#include "core.h"
#include "Modem.h"
#include "can.h"
#include "led.h"
#include "button.h"
#include "randomize.h"
#include "flash.h"
#include "work.h"
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "DataActualizator.h"

int main(void)
{
    core.initialize();
    flash.readSetup();
    flash.readSerial();
    dataActualizator.init();

    modem.initialize();
    can.initialize();
    led.initialize();
    button.initialize();
    randomize.initialize();

    Set_System();
    USB_Interrupts_Config();
    Set_USBClock();
    USB_Init();

    work.initialize();

    while (true) {
        core.handler();
        modem.handler();
        can.handler();
        button.handler();
        led.handler();
        work.handler();
    }
}
