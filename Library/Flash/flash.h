/* Define to prevent recursive inclusion --------------------------*/
#ifndef __FLASHSETUP_H
#define __FLASHSETUP_H

/* Includes -------------------------------------------------------*/
#include "n32wb452.h"
/* Define -------------------------------------------------------- */
#define FLASH_SERIAL_ADDR               0x807F000       // Length 2 KB
#define FLASH_SETUP_ADDR                0x807F800       // Length 2 KB

/* OTA staging area for a *different* device's firmware image (originally
   sized for the MBC-2 module; now also used for ПУ28-Timberline) — this
   modem's own flash just holds it temporarily while it's downloaded/
   verified over HTTP, before being replayed onto the target device over CAN
   (OmniProtocol PGN=105/106, see Timberline.cpp). Sits right below
   FLASH_SERIAL_ADDR.

   Sized 208 KB to fit ПУ28-Timberline's ~206 KB firmware image plus one
   page (2 KB) of slack — deliberately tight, not generously oversized like
   the original 160 KB (MBC-2's own 128 KB sector +32 KB headroom): the
   whole [IROM1 code][OTA meta][OTA buffer] window between IROM1's start
   (0x08030000) and FLASH_SERIAL_ADDR is only 316 KB total, so growing this
   buffer eats directly into the modem app's own code budget. If a future
   ПУ28-Timberline build grows past ~206 KB, or another target device needs
   staging, this buffer (and IROM1's declared size below) needs revisiting
   together, not just bumped in isolation.

   Confirmed on real hardware: this and FLASH_OTA_META_ADDR being merely
   *below* the app's own code was not enough to survive a reflash — Keil's
   flash download doesn't erase just the bytes the compiler actually filled,
   it erases the whole load region declared in the project's own IROM1
   size (Options for Target -> Target -> IROM1, mirrored into
   modem/Objects/modemDragonfly.sct as LR_IROM1's size field). Fixed by
   keeping IROM1's declared size well short of FLASH_OTA_META_ADDR below
   (0x1A000 = 104 KB, ending at 0x0804A000, 2 KB below FLASH_OTA_META_ADDR)
   so reflashing the app can't reach into this buffer or FLASH_SERIAL_ADDR
   above it. NOTE: this was previously declared as 0x26000 (152 KB) in a
   comment here, but modem/modemDragonfly.uvprojx's IROM1 setting (and its
   generated .sct) had never actually been shrunk to match — a real bug,
   fixed alongside this resize; verify actual code+RO+RW usage still fits
   under 104 KB after the next build (was ~78 KB under the old 152 KB
   budget). Below 0x08030000 (IROM1's own start) is this modem's own
   separate bootloader — it has an OTA buffer of its own reserved there
   already; out of scope here, a later task. FLASH_SETUP_ADDR was never at
   risk — it already sits just past where IROM1 ends either way. */
#define FLASH_OTA_BUF_ADDR              0x0804B000
#define FLASH_OTA_BUF_SIZE              (208*1024)
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

/* Self-OTA staging area — THIS modem's own new firmware image, downloaded
   and page-verified the exact same way as a target device's image above
   (see Modem::doOta(), branching on otaScratch.deviceType == VERSION_1),
   but kept in a completely separate region: the one nations-bootloader
   itself reserves below its own APP_REGION_START for exactly this purpose
   (see that project's User/Main/main.h memory map — right after the
   bootloader's own ~38 KB code). Sized 130 KB — same total size as
   APP_REGION_START's own region (main app's MAIN_PROGRAM_MAX_SIZE code
   budget + one footer page) — because as of 2026-09-04 this buffer holds
   an exact, unmodified mirror of the published self-OTA .bin: code first,
   then one trailing page in the exact same footer format the app's own
   region ends with (see main.cpp's _CRCR and nations-bootloader's
   ApplySelfOtaImage()). No separate meta/checksum record any more (that
   used to live in its own page below this buffer, written once a verified
   download finished) — see doOta()'s new "erase this whole buffer, one
   page per tick" step and readSelfOtaFooter() below for why a plain "is
   the trailing page still erased or not" check is now sufficient proof a
   download actually completed, without a dedicated record. */
#define FLASH_SELF_OTA_BUF_ADDR         0x08009800
#define FLASH_SELF_OTA_BUF_SIZE         (130*1024)
#define FLASH_SELF_OTA_PAGE_COUNT       (FLASH_SELF_OTA_BUF_SIZE / FLASH_PAGE_SIZE)

/* Software CRC16 (Modbus/CRC-16-ANSI: poly 0xA001, init 0xFFFF, no final
   XOR) — the SAME algorithm this org uses everywhere else a firmware image
   gets checksummed: nations-bootloader's own calcCrc() (validates the
   app's footer on every boot) and every bootloader's PGN=105/106 CAN-relay
   verify step. Previously this was a separate CRC32 (zlib/PKZIP) used only
   for the HTTP-download integrity check — replaced with this one so the
   OTA download path checks against the exact same algorithm the hardware
   already trusts, instead of a second, unrelated one that happened to also
   catch corruption. Shared by Flash_C::crc16OtaPage()/crc16SelfOtaPage()
   (Library/Flash/flash.cpp) and Modem::doOta() (modem/User/Modem.cpp,
   verifying a freshly-downloaded HTTPREAD chunk before it's written to
   flash). len is uint32_t (not uint16_t) so it can cover a whole staged
   OTA image in one call, not just a single page — see
   nations-bootloader's ApplySelfOtaImage() (main.cpp), which does exactly
   that against the self-OTA meta's totalCrc16. */
uint16_t flashCrc16(const uint8_t* data, uint32_t len);

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
        uint16_t crc16OtaPage(uint16_t pageIndex);

        /* Describes whatever is currently sitting in the OTA staging dump —
           version string, byte length actually staged, CRC16 over that
           whole span — written once a download finishes with every page
           individually verified (see Modem::doOta()'s cleanup step), so a
           later CAN relay onto MBC-2 (or a reboot in between) can tell
           what's really in the buffer instead of trusting stale RAM state
           or 128 KB of possibly-unrelated bytes. readOtaMeta() returns
           false (outVersion/outTotalBytes/outTotalCrc16 left untouched) if
           the sector is erased or its checksum doesn't match — same
           "erased flash reads as invalid, not garbage" contract as
           readSetup(). version must point at a buffer of at least 24 bytes
           (matches Modem::OtaScratch::version's own size). */
        /* flashBase is persisted alongside version/totalBytes/totalCrc16
           (same 2 KB meta page, just using a few more of its otherwise-
           unused bytes) so a relay attempt after a reboot doesn't need a
           fresh otaStart just to know where to write on the target device
           — see Timberline::doCanRelay()'s flashBase==0 safety check for
           what happens if this were still RAM-only (a real bricking
           incident, 2026-08-29). readOtaMeta() returns false (nothing
           written to any out-param) if the page's checksum doesn't match
           — same "erased flash reads as invalid" contract as before. */
        bool     writeOtaMeta(const char* version, uint32_t totalBytes, uint16_t totalCrc16, uint32_t flashBase);
        bool     readOtaMeta(char* outVersion, uint32_t* outTotalBytes, uint16_t* outTotalCrc16, uint32_t* outFlashBase);

        /* Self-OTA staging area (see FLASH_SELF_OTA_BUF_ADDR above) — same
           writePageAt()/readPageAt()/crc16PageAt() contract as
           writeOtaPage()/readOtaPage()/crc16OtaPage() above, just against
           the modem's own separate image buffer instead of the
           target-device one. pageIndex is 0..FLASH_SELF_OTA_PAGE_COUNT-1. */
        bool     writeSelfOtaPage(uint16_t pageIndex, const uint8_t* data);
        void     readSelfOtaPage(uint16_t pageIndex, uint8_t* outBuf);
        uint16_t crc16SelfOtaPage(uint16_t pageIndex);

        /* Erases one page of the self-OTA buffer — called by Modem::doOta(),
           once per page, one per handler() tick (see its own comment) right
           before it starts downloading a fresh self-OTA image (never
           resumed/partial for self any more), so that if THIS attempt is
           ever interrupted partway, readSelfOtaFooter() below reliably
           reads the trailing page as still-erased rather than a stale
           leftover from a previous successful download. pageIndex is
           0..FLASH_SELF_OTA_PAGE_COUNT-1, same indexing as
           writeSelfOtaPage()/readSelfOtaPage() above. */
        bool     eraseSelfOtaPage(uint16_t pageIndex);

        /* Reads the buffer's own last page (FLASH_SELF_OTA_BUF_SIZE -
           FLASH_PAGE_SIZE bytes in) as a footer, same 12-byte length+CRC16+
           version+reserved layout nations-bootloader's ApplySelfOtaImage()
           writes to APP_FOOTER_ADDR. Only accepts a stored length/CRC16
           that's internally consistent (calcCrc over that many bytes from
           the buffer's start matches what's stored) — deliberately NOT the
           build-time "debug mode" placeholder (length == 0x55555555) _CRCR
           always carries straight out of a Keil build: that only proves a
           download *reached* this page, not that its *content* (the whole
           preceding 128 KB) is actually intact right now, which could be
           checked much later than the download itself (Apply is a separate
           user action). finalizeSelfOtaFooter() below is what turns that
           placeholder into a real, freshly-computed one once a download
           actually finishes — see its own comment. A still-erased (0xFF),
           still-placeholder, or corrupted page all return false here —
           same "erased flash reads as invalid, not garbage" contract every
           other read*Meta()-style function in this file already follows.
           outVersion must point at a buffer of at least 24 bytes (matches
           Modem::SelfOtaState::stagedVersion), formatted as "V1.V2.V3.V4"
           from the footer's raw version bytes — NOT a copy of an on-flash
           string, since this footer's version field is 4 raw bytes (same
           as _CRCR's own layout), not ASCII. */
        bool     readSelfOtaFooter(char* outVersion, uint32_t* outCodeBytes);

        /* Called once by Modem::doOta(), only after every page of a self-OTA
           download (all FLASH_SELF_OTA_PAGE_COUNT of them, including the
           trailing build-time debug-placeholder page as downloaded from the
           server) has individually passed its own page-level CRC16 check.
           That per-page check only proves what arrived matched the server
           *at download time* — Apply can happen much later, so this
           replaces the placeholder with a real footer computed fresh, right
           now, over the actual 128 KB code region: erases just this one
           page (the debug placeholder was already written there straight
           from the download, so overwriting it with different bytes
           in-place without an erase first isn't safe on NOR flash) and
           writes length=MAIN_PROGRAM_MAX_SIZE (fixed — the code region is
           always this whole budget, gap-padding included, see
           readSelfOtaFooter()'s own comment on why nothing is trimmed),
           freshly-computed CRC16, and version (parsed from the given
           "V1.V2.V3.V4" string, same as Modem::OtaScratch::version).
           ApplySelfOtaImage() then re-verifies this same CRC16 immediately
           before flashing it onto the app region — closing the gap a
           corrupted/worn page between download-completion and Apply would
           otherwise slip through unnoticed. */
        bool     finalizeSelfOtaFooter(const char* version);

    private:
        /* Shared implementation behind writeOtaPage()/writeSelfOtaPage() etc.
           above — the two staging areas differ only in base address and page
           count, and the actual FLASH_Unlock()/EraseOnePage()/ProgramWord()
           sequence is exactly the same either way; keeping one implementation
           here means the two public pairs can't silently drift apart. */
        bool     writePageAt(uint32_t bufAddr, uint16_t pageCount, uint16_t pageIndex, const uint8_t* data);
        void     readPageAt(uint32_t bufAddr, uint16_t pageCount, uint16_t pageIndex, uint8_t* outBuf);
        uint16_t crc16PageAt(uint32_t bufAddr, uint16_t pageCount, uint16_t pageIndex);
        bool     writeMetaAt(uint32_t metaAddr, const char* version, uint32_t totalBytes, uint16_t totalCrc16);
        bool     readMetaAt(uint32_t metaAddr, char* outVersion, uint32_t* outTotalBytes, uint16_t* outTotalCrc16);

};
extern Flash_C flash;

#endif /* __FLASHSETUP_H */
