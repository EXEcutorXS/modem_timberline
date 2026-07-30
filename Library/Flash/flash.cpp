#include "flash.h"
#include "modem_handler.h"
#include "Modem.h"
#include "core.h"
#include <string.h>

Flash_C flash;

/* Force null-termination and check the buffer looks like a plausible
   printable string. Protects against reading raw/erased flash (0xFF) when
   an older firmware image (written before these fields existed) is read by
   newer code expecting them — the stored checksum still matches (it only
   covers bytes that were meaningful back then), so this is the only guard
   against garbage. Falls back to `fallback` and requests a rewrite if the
   content doesn't look sane. */
static void sanitizeString(char* buf, uint16_t len, const char* fallback, bool& needRewrite) {
    buf[len - 1] = '\0';
    uint16_t n = 0;
    while (n < len && buf[n]) n++;
    /* An empty string (n==0) is a legitimate, intentional value now (no
       baked-in defaults — see the Modem constructor) — only non-printable
       bytes are the real "uninitialized/erased flash" signature this is
       actually meant to catch. Treating n==0 as invalid would make this
       "fix" an already-correct empty value back to the fallback on every
       single boot, forever. */
    bool ok = true;
    for (uint16_t i = 0; ok && i < n; i++)
        if ((uint8_t)buf[i] < 0x20 || (uint8_t)buf[i] > 0x7E) ok = false;
    if (!ok) {
        strncpy(buf, fallback, len - 1);
        buf[len - 1] = '\0';
        needRewrite = true;
    }
}

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
    array[a++] = modem.useInternet ? 0 : 1;  /* stored as "onlySmsMode" byte, unchanged format */
    for (x = 0; x < 5; x++)
        array[a++] = (uint8_t)modem.pin[x];
    array[a++] = (uint8_t)modem.tempUnit;
    array[a++] = modem.faultReport ? 1 : 0;
    array[a++] = modem.cmdAck      ? 1 : 0;
    array[a++] = modem.language;
    for (x = 0; x < sizeof(modem.mqttBroker);   x++) array[a++] = (uint8_t)modem.mqttBroker[x];
    for (x = 0; x < sizeof(modem.mqttUsername); x++) array[a++] = (uint8_t)modem.mqttUsername[x];
    for (x = 0; x < sizeof(modem.mqttPassword); x++) array[a++] = (uint8_t)modem.mqttPassword[x];
    array[a++] = modem.allowRoaming ? 1 : 0;
    array[a++] = modem.force2gOnly  ? 1 : 0;
    for (x = 0; x < sizeof(modem.internetCheckUrl); x++) array[a++] = (uint8_t)modem.internetCheckUrl[x];
    for (x = 0; x < sizeof(modem.connectionLink);    x++) array[a++] = (uint8_t)modem.connectionLink[x];
    for (x = 0; x < sizeof(modem.apn);                x++) array[a++] = (uint8_t)modem.apn[x];
    for (x = 0; x < sizeof(modem.apnUsername);        x++) array[a++] = (uint8_t)modem.apnUsername[x];
    for (x = 0; x < sizeof(modem.apnPassword);        x++) array[a++] = (uint8_t)modem.apnPassword[x];

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

/* Erases the settings page without writing anything back — the next boot's
   readSetup() already treats a checksum mismatch (which an erased/all-0xFF
   page always produces) as "no valid setup", falling back to the same
   hardcoded defaults a brand-new device would get. Caller is expected to
   reboot right after (see Timberline.cpp's factory-reset button handler);
   this alone doesn't reset anything already loaded into RAM. */
void Flash_C::factoryReset(void)
{
    FLASH_Unlock();
    FLASH_EraseOnePage(FLASH_SETUP_ADDR);
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
        /* uint16_t, not uint8_t — total bytes tracked here now exceeds 255
           (connectionLink alone pushes it past that), and a uint8_t index
           would silently wrap and corrupt every field parsed after the
           wrap point. */
        uint16_t a = 0;
        for (i = 0; i < 5; i++)
            for (x = 0; x < 16; x++)
                modem.phones[i][x] = array[a++];
        core.timeZone       = array[a++];
        modem.useInternet = (array[a++] != 1);  /* byte is "onlySmsMode"; invert into useInternet */
        for (x = 0; x < 5; x++) modem.pin[x] = (char)array[a++];
        modem.pin[4] = '\0';
        modem.tempUnit   = array[a++];
        modem.faultReport = (array[a++] == 1);
        modem.cmdAck      = (array[a++] == 1);
        modem.language     = array[a++];
        for (x = 0; x < sizeof(modem.mqttBroker);   x++) modem.mqttBroker[x]   = (char)array[a++];
        for (x = 0; x < sizeof(modem.mqttUsername); x++) modem.mqttUsername[x] = (char)array[a++];
        for (x = 0; x < sizeof(modem.mqttPassword); x++) modem.mqttPassword[x] = (char)array[a++];
        uint8_t rawAllowRoaming = array[a++];
        modem.allowRoaming = (rawAllowRoaming == 1);
        uint8_t rawForce2gOnly = array[a++];
        modem.force2gOnly = (rawForce2gOnly == 1);
        for (x = 0; x < sizeof(modem.internetCheckUrl); x++) modem.internetCheckUrl[x] = (char)array[a++];
        for (x = 0; x < sizeof(modem.connectionLink);    x++) modem.connectionLink[x]    = (char)array[a++];
        for (x = 0; x < sizeof(modem.apn);                x++) modem.apn[x]              = (char)array[a++];
        for (x = 0; x < sizeof(modem.apnUsername);        x++) modem.apnUsername[x]      = (char)array[a++];
        for (x = 0; x < sizeof(modem.apnPassword);        x++) modem.apnPassword[x]      = (char)array[a++];

        bool needRewrite = false;
        /* allowRoaming/force2gOnly didn't exist in older firmware images —
           same stale/erased-byte situation as mqttBroker/Username/Password
           below, just for a bool instead of a string. */
        if (rawAllowRoaming > 1) { modem.allowRoaming = false; needRewrite = true; }
        if (rawForce2gOnly > 1) { modem.force2gOnly = false; needRewrite = true; }
        if (strlen(modem.pin) != 4) {
            modem.pin[0]='1'; modem.pin[1]='2'; modem.pin[2]='3'; modem.pin[3]='4'; modem.pin[4]='\0';
            needRewrite = true;
        }
        if (modem.language > 1) { modem.language = 0; needRewrite = true; } /* firmware update from an image with no language byte */
        /* mqttBroker/Username/Password: fields didn't exist in older firmware
           images — the checksum still validates (it only covers what was
           meaningful then), so what we just read could be raw erased/stale
           flash bytes rather than a real string. sanitizeString() catches
           that; fallback is empty, not a real broker/account — see the
           Modem constructor for why there's no baked-in default. */
        sanitizeString(modem.mqttBroker,   sizeof(modem.mqttBroker),   "", needRewrite);
        sanitizeString(modem.mqttUsername, sizeof(modem.mqttUsername), "", needRewrite);
        sanitizeString(modem.mqttPassword, sizeof(modem.mqttPassword), "", needRewrite);
        /* internetCheckUrl didn't exist in older firmware images either —
           same stale/erased-byte risk, but unlike mqttBroker/Username/
           Password an empty value here isn't a sensible fallback (it's a
           check URL, not a credential) — fall back to the same default the
           constructor uses for a genuinely fresh device. */
        sanitizeString(modem.internetCheckUrl, sizeof(modem.internetCheckUrl), "http://google.com", needRewrite);
        /* connectionLink is brand new too — empty is a perfectly legitimate
           "no getlink sent yet" state, same as mqttBroker/Username/Password. */
        sanitizeString(modem.connectionLink, sizeof(modem.connectionLink), "", needRewrite);
        /* apn/apnUsername/apnPassword: brand new fields, empty = auto —
           same reasoning as connectionLink. */
        sanitizeString(modem.apn, sizeof(modem.apn), "", needRewrite);
        sanitizeString(modem.apnUsername, sizeof(modem.apnUsername), "", needRewrite);
        sanitizeString(modem.apnPassword, sizeof(modem.apnPassword), "", needRewrite);
        if (needRewrite) writeSetup();
    } else {
        modem.useInternet = true;
        modem.allowRoaming = false;
        modem.force2gOnly = false;
        modem.tempUnit    = 0;
        modem.faultReport = true;
        modem.cmdAck      = true;
        modem.language    = 0;
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
