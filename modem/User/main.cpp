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

/* Совместимость с nations-bootloader: он читает метаданные из ADDRESS_CRC.
   Страница 0x0804A000..0x0804A800 (последняя страница IROM1) зарезервирована
   под футер — код и таблица векторов начинаются с самого начала региона
   (MAIN_PROGRAM_START_ADDRESS=0x0802A000), линкеру не нужно ничего
   "обтекать" внутри кода (см. modemDragonfly.uvprojx IROM1 и main.h).
   Футер перенесён в конец региона 2026-09-04 (было в начале) — таблица
   векторов теперь стоит там же, где у всех остальных типов устройств: в
   самом начале flash-региона приложения, без зарезервированной страницы
   перед ней.
   lenMain=0x55555555 → "debug mode" → загрузчик запускает приложение без
   проверки CRC (тот же приём, что и в PU28-Timberline/User/Main/main.cpp). */
const uint8_t _CRCR[FLASH_PAGE_SIZE] __attribute__((at(ADDRESS_CRC))) =
{
    0x55, 0x55, 0x55, 0x55,
    0x55, 0x55,
    VERSION_1, VERSION_2, VERSION_3, VERSION_4,
    0x00
};

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
