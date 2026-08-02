/* Define to prevent recursive inclusion --------------------------*/
#ifndef __FLASHSETUP_H
#define __FLASHSETUP_H

/* Includes -------------------------------------------------------*/
#include "n32wb452.h"
/* Define -------------------------------------------------------- */
#define FLASH_SERIAL_ADDR               0x807F000       // Length 2 KB
#define FLASH_SETUP_ADDR                0x807F800       // Length 2 KB

/* OTA staging area for a *different* device's firmware image (the MBC-2
   module) — this modem's own flash just holds it temporarily while it's
   downloaded/verified over HTTP, before being replayed onto MBC-2 over CAN
   (OmniProtocol PGN=105/106, see Timberline.cpp). Sits right below
   FLASH_SERIAL_ADDR.

   Sized 160 KB, not the 128 KB MBC-2's own flash sector 5 (its "Main
   Program" region) would strictly need — MBC-2's real current build is
   already ~112.5 KB (Code+RO-data+RW-data from its own build log), leaving
   only ~15 KB of slack in a single 128 KB sector before it'd need sector 6
   too. 160 KB buys about 32 KB more headroom for that growth without
   having to revisit this buffer again immediately — see Timberline.cpp's
   doCanRelay() case 2 for the sector 5+6 erase and MBC-2's own real sector
   map (confirmed by the user, not just inferred from its .sct).

   Confirmed on real hardware: this and FLASH_OTA_META_ADDR being merely
   *below* the app's own code was not enough to survive a reflash — Keil's
   flash download doesn't erase just the bytes the compiler actually filled,
   it erases the whole load region declared in the project's own IROM1
   size (Options for Target -> Target -> IROM1, mirrored into
   modem/Objects/modemDragonfly.sct as LR_IROM1's size field). Fixed by
   keeping IROM1's declared size well short of FLASH_OTA_META_ADDR below
   (currently 0x26000 = 152 KB, ending at 0x08056000 — actual usage is
   ~78 KB, still comfortable growth room) so reflashing the app can't reach
   into this buffer or FLASH_SERIAL_ADDR above it. Below 0x08030000
   (IROM1's own start) is this modem's own separate bootloader — it has an
   OTA buffer of its own reserved there already; out of scope here, a
   later task. FLASH_SETUP_ADDR was never at risk — it already sits just
   past where IROM1 ends either way. */
#define FLASH_OTA_BUF_ADDR              0x08057000
#define FLASH_OTA_BUF_SIZE              (160*1024)
#define FLASH_PAGE_SIZE                 2048
#define FLASH_OTA_PAGE_COUNT            (FLASH_OTA_BUF_SIZE / FLASH_PAGE_SIZE)

/* One page, immediately below the OTA staging dump itself — deliberately a
   separate sector from FLASH_SETUP_ADDR's user settings, not folded into
   writeSetup()'s blob: this describes what's sitting in the OTA buffer
   right next to it, not modem configuration, and the two are erased/
   rewritten on completely different triggers (a firmware download landing
   vs. a settings change). See the big comment above FLASH_OTA_BUF_ADDR —
   this only stays reflash-safe now that IROM1's declared size stops short
   of it. */
#define FLASH_OTA_META_ADDR             (FLASH_OTA_BUF_ADDR - FLASH_PAGE_SIZE)

/* Software CRC32 (poly 0xEDB88320, standard zlib/PKZIP/IEEE 802.3 reflected
   variant) — shared by Flash_C::crc32OtaPage() (Library/Flash/flash.cpp)
   and Modem::doOta() (modem/User/Modem.cpp, verifying a freshly-downloaded
   HTTPREAD chunk before it's written to flash), so both use the exact same
   implementation instead of two copies that could silently drift apart.
   Matches host/tools/make_firmware_crc.js's independent JS implementation
   of the same standard algorithm. len is uint32_t (not uint16_t) so it can
   cover the whole staged OTA image (up to 128 KB) in one call, not just a
   single page. */
uint32_t flashCrc32(const uint8_t* data, uint32_t len);

/* Classes --------------------------------------------------------*/
class Flash_C
{
    public:
        Flash_C(void);
        void initialize(void);
        void handler(void);
        void writeSetup(void);
        void readSetup(void);
        void factoryReset(void);
        void writeSerial(void);
        void readSerial(void);
        uint8_t getHardwareVersion(void);

        /* OTA staging area (see FLASH_OTA_BUF_ADDR above) — pageIndex is
           0..FLASH_OTA_PAGE_COUNT-1, data/outBuf must be exactly
           FLASH_PAGE_SIZE (2048) bytes. Unlike writeSetup()/writeSerial(),
           writeOtaPage() checks every FLASH_STS return value — a silently
           dropped error here means flashing a bad image onto MBC-2. */
        bool     writeOtaPage(uint16_t pageIndex, const uint8_t* data);
        void     readOtaPage(uint16_t pageIndex, uint8_t* outBuf);
        uint32_t crc32OtaPage(uint16_t pageIndex);

        /* Describes whatever is currently sitting in the OTA staging dump —
           version string, byte length actually staged, CRC32 over that
           whole span — written once a download finishes with every page
           individually verified (see Modem::doOta()'s cleanup step), so a
           later CAN relay onto MBC-2 (or a reboot in between) can tell
           what's really in the buffer instead of trusting stale RAM state
           or 128 KB of possibly-unrelated bytes. readOtaMeta() returns
           false (outVersion/outTotalBytes/outTotalCrc32 left untouched) if
           the sector is erased or its checksum doesn't match — same
           "erased flash reads as invalid, not garbage" contract as
           readSetup(). version must point at a buffer of at least 24 bytes
           (matches Modem::OtaScratch::version's own size). */
        bool     writeOtaMeta(const char* version, uint32_t totalBytes, uint32_t totalCrc32);
        bool     readOtaMeta(char* outVersion, uint32_t* outTotalBytes, uint32_t* outTotalCrc32);

    private:
        

};
extern Flash_C flash;

#endif /* __FLASHSETUP_H */
