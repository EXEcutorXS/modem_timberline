#include "flash.h"
#include "Converter.h"
#include "modem_handler.h"
#include "Modem.h"
#include "core.h"
#include <string.h>

Flash_C flash;

Flash_C::Flash_C(void) {}
void Flash_C::initialize(void) {}
void Flash_C::handler(void) {}

void Flash_C::writeSetup(void)
{
    uint32_t a, N;
    uint8_t x, array[512], i;

    FLASH_Unlock();
    for (a = 0; a < 512; a++)
        array[a] = *(__IO uint8_t*)(FLASH_SETUP_ADDR + a);
    FLASH_EraseOnePage(FLASH_SETUP_ADDR);

    a = 0;
    for (i = 0; i < 5; i++)
        for (x = 0; x < 16; x++)
            array[a++] = modem.phones[i][x];
    array[a++] = core.timeZone;
    array[a++] = modem.isOnlySmsMode ? 1 : 0;
    for (x = 0; x < 5; x++)
        array[a++] = (uint8_t)modem.pin[x];

    x = 0;
    for (a = 0; a < 511; a++) x += array[a];
    array[511] = x;

    for (a = 0; a < 512; a += 4) {
        N  = (uint32_t)array[a];
        N |= (uint32_t)array[a+1] << 8;
        N |= (uint32_t)array[a+2] << 16;
        N |= (uint32_t)array[a+3] << 24;
        FLASH_ProgramWord(FLASH_SETUP_ADDR + a, N);
    }
    FLASH_Lock();
}

void Flash_C::readSetup(void)
{
    uint8_t x, array[512], i;
    uint16_t idx;

    x = 0;
    for (idx = 0; idx < 511; idx++) {
        array[idx] = *(__IO uint8_t*)(FLASH_SETUP_ADDR + idx);
        x += array[idx];
    }

    if (x == *(__IO uint8_t*)(FLASH_SETUP_ADDR + 511)) {
        uint8_t a = 0;
        for (i = 0; i < 5; i++)
            for (x = 0; x < 16; x++)
                modem.phones[i][x] = array[a++];
        core.timeZone       = array[a++];
        modem.isOnlySmsMode = (array[a++] == 1);
        for (x = 0; x < 5; x++) modem.pin[x] = (char)array[a++];
        modem.pin[4] = '\0';
        if (strlen(modem.pin) != 4) {
            modem.pin[0]='1'; modem.pin[1]='2'; modem.pin[2]='3'; modem.pin[3]='4'; modem.pin[4]='\0';
            writeSetup();
        }
    } else {
        modem.isOnlySmsMode = true;
        core.timeZone = 3;
        modem.pin[0]='1'; modem.pin[1]='2'; modem.pin[2]='3'; modem.pin[3]='4'; modem.pin[4]='\0';
        writeSetup();
    }
}

void Flash_C::writeSerial(void)
{
    uint32_t a, N;
    uint8_t array[16], i;

    FLASH_Unlock();
    FLASH_EraseOnePage(FLASH_SERIAL_ADDR);
    for (i = 0; i < 16; i++) array[i] = serialNumberModem[i];
    for (a = 0; a < 16; a += 4) {
        N  = (uint32_t)array[a];
        N |= (uint32_t)array[a+1] << 8;
        N |= (uint32_t)array[a+2] << 16;
        N |= (uint32_t)array[a+3] << 24;
        FLASH_ProgramWord(FLASH_SERIAL_ADDR + a, N);
    }
    FLASH_Lock();
}

void Flash_C::readSerial(void)
{
    for (uint16_t i = 0; i < 16; i++)
        serialNumberModem[i] = *(__IO uint8_t*)(FLASH_SERIAL_ADDR + i);
}

uint8_t Flash_C::getHardwareVersion(void)
{
    return *(__IO uint8_t*)(0x801C00A);
}
