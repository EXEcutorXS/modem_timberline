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
            array[a++] = modem.config.phones[i][x];
    array[a++] = core.timeZone;
    array[a++] = modem.config.useInternet ? 0 : 1;  /* stored as "onlySmsMode" byte, unchanged format */
    for (x = 0; x < 5; x++)
        array[a++] = (uint8_t)modem.config.pin[x];
    array[a++] = (uint8_t)modem.config.tempUnit;
    array[a++] = modem.config.faultReport ? 1 : 0;
    array[a++] = modem.config.cmdAck      ? 1 : 0;
    array[a++] = modem.config.language;
    for (x = 0; x < sizeof(modem.mqtt.broker);   x++) array[a++] = (uint8_t)modem.mqtt.broker[x];
    for (x = 0; x < sizeof(modem.mqtt.username); x++) array[a++] = (uint8_t)modem.mqtt.username[x];
    for (x = 0; x < sizeof(modem.mqtt.password); x++) array[a++] = (uint8_t)modem.mqtt.password[x];
    array[a++] = modem.config.allowRoaming ? 1 : 0;
    array[a++] = modem.config.force2gOnly  ? 1 : 0;
    for (x = 0; x < sizeof(modem.internet.internetCheckUrl); x++) array[a++] = (uint8_t)modem.internet.internetCheckUrl[x];
    for (x = 0; x < sizeof(modem.internet.connectionLink);    x++) array[a++] = (uint8_t)modem.internet.connectionLink[x];
    for (x = 0; x < sizeof(modem.internet.apn);                x++) array[a++] = (uint8_t)modem.internet.apn[x];
    for (x = 0; x < sizeof(modem.internet.apnUsername);        x++) array[a++] = (uint8_t)modem.internet.apnUsername[x];
    for (x = 0; x < sizeof(modem.internet.apnPassword);        x++) array[a++] = (uint8_t)modem.internet.apnPassword[x];

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
                modem.config.phones[i][x] = array[a++];
        core.timeZone       = array[a++];
        modem.config.useInternet = (array[a++] != 1);  /* byte is "onlySmsMode"; invert into useInternet */
        for (x = 0; x < 5; x++) modem.config.pin[x] = (char)array[a++];
        modem.config.pin[4] = '\0';
        modem.config.tempUnit   = array[a++];
        modem.config.faultReport = (array[a++] == 1);
        modem.config.cmdAck      = (array[a++] == 1);
        modem.config.language     = array[a++];
        for (x = 0; x < sizeof(modem.mqtt.broker);   x++) modem.mqtt.broker[x]   = (char)array[a++];
        for (x = 0; x < sizeof(modem.mqtt.username); x++) modem.mqtt.username[x] = (char)array[a++];
        for (x = 0; x < sizeof(modem.mqtt.password); x++) modem.mqtt.password[x] = (char)array[a++];
        uint8_t rawAllowRoaming = array[a++];
        modem.config.allowRoaming = (rawAllowRoaming == 1);
        uint8_t rawForce2gOnly = array[a++];
        modem.config.force2gOnly = (rawForce2gOnly == 1);
        for (x = 0; x < sizeof(modem.internet.internetCheckUrl); x++) modem.internet.internetCheckUrl[x] = (char)array[a++];
        for (x = 0; x < sizeof(modem.internet.connectionLink);    x++) modem.internet.connectionLink[x]    = (char)array[a++];
        for (x = 0; x < sizeof(modem.internet.apn);                x++) modem.internet.apn[x]              = (char)array[a++];
        for (x = 0; x < sizeof(modem.internet.apnUsername);        x++) modem.internet.apnUsername[x]      = (char)array[a++];
        for (x = 0; x < sizeof(modem.internet.apnPassword);        x++) modem.internet.apnPassword[x]      = (char)array[a++];

        bool needRewrite = false;
        /* allowRoaming/force2gOnly didn't exist in older firmware images —
           same stale/erased-byte situation as mqttBroker/Username/Password
           below, just for a bool instead of a string. */
        if (rawAllowRoaming > 1) { modem.config.allowRoaming = false; needRewrite = true; }
        if (rawForce2gOnly > 1) { modem.config.force2gOnly = false; needRewrite = true; }
        if (strlen(modem.config.pin) != 4) {
            modem.config.pin[0]='1'; modem.config.pin[1]='2'; modem.config.pin[2]='3'; modem.config.pin[3]='4'; modem.config.pin[4]='\0';
            needRewrite = true;
        }
        if (modem.config.language > 1) { modem.config.language = 0; needRewrite = true; } /* firmware update from an image with no language byte */
        /* mqttBroker/Username/Password: fields didn't exist in older firmware
           images — the checksum still validates (it only covers what was
           meaningful then), so what we just read could be raw erased/stale
           flash bytes rather than a real string. sanitizeString() catches
           that; fallback is empty, not a real broker/account — see the
           Modem constructor for why there's no baked-in default. */
        sanitizeString(modem.mqtt.broker,   sizeof(modem.mqtt.broker),   "", needRewrite);
        sanitizeString(modem.mqtt.username, sizeof(modem.mqtt.username), "", needRewrite);
        sanitizeString(modem.mqtt.password, sizeof(modem.mqtt.password), "", needRewrite);
        /* internetCheckUrl didn't exist in older firmware images either —
           same stale/erased-byte risk, but unlike mqttBroker/Username/
           Password an empty value here isn't a sensible fallback (it's a
           check URL, not a credential) — fall back to the same default the
           constructor uses for a genuinely fresh device. */
        sanitizeString(modem.internet.internetCheckUrl, sizeof(modem.internet.internetCheckUrl), "http://example.com", needRewrite);
        /* connectionLink is brand new too — empty is a perfectly legitimate
           "no getlink sent yet" state, same as mqttBroker/Username/Password. */
        sanitizeString(modem.internet.connectionLink, sizeof(modem.internet.connectionLink), "", needRewrite);
        /* apn/apnUsername/apnPassword: brand new fields, empty = auto —
           same reasoning as connectionLink. */
        sanitizeString(modem.internet.apn, sizeof(modem.internet.apn), "", needRewrite);
        sanitizeString(modem.internet.apnUsername, sizeof(modem.internet.apnUsername), "", needRewrite);
        sanitizeString(modem.internet.apnPassword, sizeof(modem.internet.apnPassword), "", needRewrite);
        if (needRewrite) writeSetup();
    } else {
        modem.config.useInternet = true;
        modem.config.allowRoaming = false;
        modem.config.force2gOnly = false;
        modem.config.tempUnit    = 0;
        modem.config.faultReport = false;
        modem.config.cmdAck      = true;
        modem.config.language    = 0;
        core.timeZone = 3;
        modem.config.pin[0]='1'; modem.config.pin[1]='2'; modem.config.pin[2]='3'; modem.config.pin[3]='4'; modem.config.pin[4]='\0';
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

/* See the declaration in flash.h — shared with Modem::doOta(). Table-free
   bitwise form — called at most a few dozen times per OTA (once per flash
   page, plus once per downloaded HTTP chunk, plus once over the whole
   staged image at the end), not worth a 1 KB lookup table for that. */
uint32_t flashCrc32(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320 & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

bool Flash_C::writeOtaPage(uint16_t pageIndex, const uint8_t* data)
{
    if (pageIndex >= FLASH_OTA_PAGE_COUNT) return false;
    uint32_t pageAddr = FLASH_OTA_BUF_ADDR + (uint32_t)pageIndex * FLASH_PAGE_SIZE;

    FLASH_Unlock();
    bool ok = (FLASH_EraseOnePage(pageAddr) == FLASH_COMPL);
    if (ok) {
        for (uint32_t a = 0; a < FLASH_PAGE_SIZE; a += 4) {
            uint32_t word = (uint32_t)data[a] | ((uint32_t)data[a+1] << 8)
                           | ((uint32_t)data[a+2] << 16) | ((uint32_t)data[a+3] << 24);
            if (FLASH_ProgramWord(pageAddr + a, word) != FLASH_COMPL) { ok = false; break; }
        }
    }
    FLASH_Lock();
    return ok;
}

void Flash_C::readOtaPage(uint16_t pageIndex, uint8_t* outBuf)
{
    if (pageIndex >= FLASH_OTA_PAGE_COUNT) return;
    uint32_t pageAddr = FLASH_OTA_BUF_ADDR + (uint32_t)pageIndex * FLASH_PAGE_SIZE;
    for (uint32_t a = 0; a < FLASH_PAGE_SIZE; a++)
        outBuf[a] = *(__IO uint8_t*)(pageAddr + a);
}

uint32_t Flash_C::crc32OtaPage(uint16_t pageIndex)
{
    if (pageIndex >= FLASH_OTA_PAGE_COUNT) return 0;
    uint32_t pageAddr = FLASH_OTA_BUF_ADDR + (uint32_t)pageIndex * FLASH_PAGE_SIZE;
    return flashCrc32((const uint8_t*)pageAddr, FLASH_PAGE_SIZE);
}

/* Record layout (36 bytes, same "explicit fields + trailing sum byte"
   convention as writeSetup()/readSetup() above — not a struct memcpy, so
   the on-flash format never silently shifts if the struct's own padding
   ever changes): version[24], totalBytes (4 bytes LE), totalCrc32 (4 bytes
   LE), 3 reserved/padding bytes, checksum byte. Only ever needs the one
   page FLASH_OTA_META_ADDR reserves. */
bool Flash_C::writeOtaMeta(const char* version, uint32_t totalBytes, uint32_t totalCrc32)
{
    uint8_t array[36];
    uint16_t a = 0, x;

    for (x = 0; x < 24; x++) array[a++] = (uint8_t)version[x];
    array[a++] = (uint8_t)(totalBytes);       array[a++] = (uint8_t)(totalBytes >> 8);
    array[a++] = (uint8_t)(totalBytes >> 16); array[a++] = (uint8_t)(totalBytes >> 24);
    array[a++] = (uint8_t)(totalCrc32);       array[a++] = (uint8_t)(totalCrc32 >> 8);
    array[a++] = (uint8_t)(totalCrc32 >> 16); array[a++] = (uint8_t)(totalCrc32 >> 24);
    array[a++] = 0; array[a++] = 0; array[a++] = 0; /* reserved */

    uint8_t sum = 0;
    for (a = 0; a < 35; a++) sum += array[a];
    array[35] = sum;

    FLASH_Unlock();
    bool ok = (FLASH_EraseOnePage(FLASH_OTA_META_ADDR) == FLASH_COMPL);
    if (ok) {
        for (a = 0; a < 36; a += 4) {
            uint32_t word = (uint32_t)array[a] | ((uint32_t)array[a+1] << 8)
                           | ((uint32_t)array[a+2] << 16) | ((uint32_t)array[a+3] << 24);
            if (FLASH_ProgramWord(FLASH_OTA_META_ADDR + a, word) != FLASH_COMPL) { ok = false; break; }
        }
    }
    FLASH_Lock();
    return ok;
}

bool Flash_C::readOtaMeta(char* outVersion, uint32_t* outTotalBytes, uint32_t* outTotalCrc32)
{
    uint8_t array[36];
    uint16_t a;
    uint8_t sum = 0;

    for (a = 0; a < 36; a++) array[a] = *(__IO uint8_t*)(FLASH_OTA_META_ADDR + a);
    for (a = 0; a < 35; a++) sum += array[a];
    if (sum != array[35]) return false;

    for (a = 0; a < 24; a++) outVersion[a] = (char)array[a];
    *outTotalBytes  = (uint32_t)array[24] | ((uint32_t)array[25] << 8) | ((uint32_t)array[26] << 16) | ((uint32_t)array[27] << 24);
    *outTotalCrc32  = (uint32_t)array[28] | ((uint32_t)array[29] << 8) | ((uint32_t)array[30] << 16) | ((uint32_t)array[31] << 24);
    return true;
}
