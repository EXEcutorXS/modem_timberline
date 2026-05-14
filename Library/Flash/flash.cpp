#include "flash.h"
#include "Converter.h"
#include "modem_handler.h"
#include "gsm.h"
#include "core.h"

Flash_C flash;

Flash_C::Flash_C(void)
{
}

void Flash_C::initialize(void)
{
}

void Flash_C::handler(void)
{
}

void Flash_C::writeSetup(void)
{
    uint32_t a, N;
    uint8_t x, array[512], i;

    FLASH_Unlock();
    for (a = 0; a < 512; a++) {
        array[a] = *(__IO uint8_t*)(FLASH_SETUP_ADDR + a);
    }
    FLASH_EraseOnePage(FLASH_SETUP_ADDR);

    a = 0;
    for (i = 0; i < 5; i++) {
        for (x = 0; x < 16; x++) {
            array[a++] = gsm.phones[i][x];
        }
    }
    array[a++] = core.timeZone;
    array[a++] = gsm.isOnlySmsMode ? 1 : 0;

    x = 0;
    for (a = 0; a < 511; a++) {
        x += array[a];
    }
    array[511] = x;

    for (a = 0; a < 512; a += 4) {
        N  = (uint32_t)array[a];
        N |= (uint32_t)array[a + 1] << 8;
        N |= (uint32_t)array[a + 2] << 16;
        N |= (uint32_t)array[a + 3] << 24;
        FLASH_ProgramWord(FLASH_SETUP_ADDR + a, N);
    }
    FLASH_Lock();
}

void Flash_C::readSetup(void)
{
    uint8_t x, array[512], i;
    uint16_t idx;
    bool valid = false;

    x = 0;
    for (idx = 0; idx < 511; idx++) {
        array[idx] = *(__IO uint8_t*)(FLASH_SETUP_ADDR + idx);
        x += array[idx];
    }
    if (x == *(__IO uint8_t*)(FLASH_SETUP_ADDR + 511)) {
        valid = true;
    }

    if (valid) {
        uint8_t a = 0;
        for (i = 0; i < 5; i++) {
            for (x = 0; x < 16; x++) {
                gsm.phones[i][x] = array[a++];
            }
        }
        core.timeZone = array[a++];
        gsm.isOnlySmsMode = (array[a++] == 1);
    } else {
        gsm.isOnlySmsMode = true;
        core.timeZone = 3;
        writeSetup();
    }
}

void Flash_C::writeSerial(void)
{
    uint32_t a, N;
    uint8_t array[16], i;

    FLASH_Unlock();
    FLASH_EraseOnePage(FLASH_SERIAL_ADDR);

    for (i = 0; i < 16; i++) {
        array[i] = serialNumberModem[i];
    }

    for (a = 0; a < 16; a += 4) {
        N  = (uint32_t)array[a];
        N |= (uint32_t)array[a + 1] << 8;
        N |= (uint32_t)array[a + 2] << 16;
        N |= (uint32_t)array[a + 3] << 24;
        FLASH_ProgramWord(FLASH_SERIAL_ADDR + a, N);
    }
    FLASH_Lock();
}

void Flash_C::readSerial(void)
{
    uint16_t i;
    for (i = 0; i < 16; i++) {
        serialNumberModem[i] = *(__IO uint8_t*)(FLASH_SERIAL_ADDR + i);
    }
}

uint8_t Flash_C::getHardwareVersion(void)
{
    return *(__IO uint8_t*)(0x801C00A);
}
