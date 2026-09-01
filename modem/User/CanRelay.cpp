#include "CanRelay.h"
#include "Modem.h"
#include "flash.h"
#include "log.h"
#include "core.h"
#include <string.h>

CanRelay canRelay;

/* Always-visible USB debug output for the relay, same idiom as Modem.cpp's
   logOtaFail()/logOtaInfo() for the HTTP download side — log_error()/
   log_info() are unconditional (unlike log_at(), not gated by the current
   log-level debug command), and there's no printf here, so lines are
   hand-built via appendUint()/appendHex(). Local copies of appendUint()/
   canId() — Timberline.cpp keeps its own (used well beyond the relay), not
   worth sharing a two-line helper across a header just for this. */
static int appendUint(char* buf, int n, uint32_t v) {
    char tmp[10]; int t = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0 && t < 10) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t > 0) buf[n++] = tmp[--t];
    return n;
}
static int appendHex(char* buf, int n, uint32_t v) {
    buf[n++] = '0'; buf[n++] = 'x';
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        uint8_t nib = (uint8_t)((v >> shift) & 0xF);
        buf[n++] = (char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10));
    }
    return n;
}
static void logRelayInfo(const char* msg) {
    log_info("[CANRELAY] ");
    log_info(msg);
    log_info("\r\n");
}
static void logRelayInfoNum(const char* label, uint32_t v) {
    static char buf[64];
    int n = 0;
    const char* pre = "[CANRELAY] ";
    while (*pre) buf[n++] = *pre++;
    for (const char* p = label; *p; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = appendUint(buf, n, v);
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_info(buf);
}
/* extraIsHex: addresses/CRCs read far better in hex than the decimal
   appendUint() everywhere else in this codebase uses — worth the one-off
   inconsistency here since address/CRC mismatches are exactly what this
   is for diagnosing. */
static void logRelayFail(uint8_t stepNum, const char* reason, uint32_t extra, bool extraIsHex) {
    static char buf[112];
    int n = 0;
    const char* pre = "[CANRELAY] FAIL step ";
    while (*pre) buf[n++] = *pre++;
    n = appendUint(buf, n, stepNum);
    buf[n++] = ' '; buf[n++] = '(';
    for (const char* p = reason; *p && n < 90; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = extraIsHex ? appendHex(buf, n, extra) : appendUint(buf, n, extra);
    buf[n++] = ')'; buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_error(buf);
}

static uint32_t canId(uint16_t pgn, uint8_t toType, uint8_t toAddress) {
    return ((uint32_t)pgn<<20) | ((uint32_t)toType<<13) | ((uint32_t)toAddress<<10)
         | ((uint32_t)can.idType<<3) | can.idAddress;
}

void CanRelay::start(uint8_t targetType, uint8_t targetAddress) {
    this->targetType = targetType;
    this->targetAddress = targetAddress;
    startRequested = true;
}

/* Picks out only the two PGN groups the relay cares about — everything
   else (heater/zone/device telemetry, generic discovery, ...) is
   Timberline::ProcessCanMessage()'s job, called separately by
   Can::processCanRxMessage() on the same incoming frame. */
void CanRelay::ProcessCanMessage(CanRxMessage* msg)
{
    uint16_t pgn = (msg->ExtId>>20)&0x1FF;
    uint8_t  TransType = (msg->ExtId>>3)&127;
    uint8_t* D = msg->Data;

    if (pgn == 18) {
        if (TransType != 123) return;  //  not a bootloader-mode announcement
        /* REVERTED 2026-08-29 (same day as added): tried gating this on
           D[0]==123 on the theory that a bootloader-mode device sends two
           PGN=18 announcements (its own version vs. an app-version echo)
           and only the D[0]==123 one was meaningful. Wrong on real
           hardware — with the filter in place, bootloaderSeen never became
           true at all (15s timeout every time), meaning this specific
           bootloader's *only* PGN=18 reply has D[0] equal to the real
           product type (125), not 123. So the earlier "125.0.0.16" capture
           (D[0..3] = {125,0,0,16}) was this bootloader's genuine, only
           self-report — not a stray app-echo competing with a real one.
           The original "bootloader-algorithm-unknown" failure this filter
           was meant to fix has a different real cause: see
           [[can_relay_gen3_bootloader]] memory note — not a bad version
           capture. Don't re-add a D[0] filter here without confirming against the
           actual protocol doc or a live CAN sniff first; the assumption
           that D[0] encodes product type doesn't necessarily hold for
           every PGN/context. */
        bootloaderSeen = true;
        memcpy(bootloaderVersion, D, 4);
        return;
    }

    if (pgn == 105 || pgn == 110) {  //  Bootloader flash sub-protocol responses (gen2/gen3
                                      //  respectively) — see handler(). Same sub-command
                                      //  numbering and byte layout in both PGNs (gen3 only
                                      //  differs in which checksum the *bootloader itself*
                                      //  computed into checkCrc — still 4 bytes at D[4..7]
                                      //  either way), so one parser covers both.
        if (TransType != 123) return;
        switch (D[0]) {
        case 1: //  echo of the set-address request (sub0)
            setAddrEcho = ((uint32_t)D[1]<<24)|((uint32_t)D[2]<<16)|((uint32_t)D[3]<<8)|D[4];
            setAddrGotResp = true;
            break;
        case 3: //  length+CRC of what's currently in the bootloader's RAM buffer (sub2 query)
            checkLen = ((uint32_t)D[1]<<16)|((uint32_t)D[2]<<8)|D[3];
            checkCrc = ((uint32_t)D[4]<<24)|((uint32_t)D[5]<<16)|((uint32_t)D[6]<<8)|D[7];
            checkGotResp = true;
            break;
        case 5: //  result of the RAM->flash commit (sub4)
            flashResult = D[1];
            flashGotResp = true;
            break;
        case 7: //  result of the erase-memory request (sub6)
            eraseResult = D[1];
            eraseGotResp = true;
            break;
        }
    }
}

/* MBC-2's own flash image starts at 0x08020000 (confirmed against its
   scatter file, HCU-Timberline2/Objects/hcu.sct: LR_IROM1 0x08020000
   0x0001FFC0 + LR_IROM2 0x0803FFC0 0x40 — together exactly the 128 KB
   region Modem::ota's staging buffer mirrors byte-for-byte from offset 0),
   and the bootloader always identifies itself as device type 123
   regardless of the app's own type (125 for MBC-2) — see OmniProtocol.pdf's
   device-type table and PGN sections. Flash base address is no longer
   hardcoded here — it comes from modem.ota.flashBase, fetched from the
   target firmware's own profile on the server (see
   Modem::doFetchProfile()), so this same relay logic works for any device
   type, not just MBC-2.

   2026-08-30: split into separate per-generation state machines
   (handleGen2()/handleGen3() below), sharing only detection (handleDetect())
   and the two finish helpers (finishFailed()/finishSuccess()). Used to be
   one shared switch with `algorithm == 3 ? X : Y` branches sprinkled through
   nearly every case — technically less code, but genuinely harder to read
   than two straightforward, independently-followable sequences (user's
   call: "куда чище было бы реализовать 2 стейт машины, хоть и длиннее
   исходник"). Keep it that way — if a THIRD generation ever shows up,
   add handleGen4() rather than reintroducing branches into these two. */
#define CAN_RELAY_FRAGMENT_SIZE 512
#define CAN_RELAY_MAX_RETRIES 10
/* Backstop only — every individual sub-step already has its own timeout+
   retry budget (CAN_RELAY_MAX_RETRIES, or the fixed 15s/8s waits below),
   so a single stuck step already fails out well before this. This just
   bounds the *whole* relay in case something strings together retries
   indefinitely in a way no single step's own budget catches — e.g. a
   marginal bus where every fragment individually "succeeds" but only
   after several retries each, adding up. 20 minutes is generous for a
   full ~208 KB image at this fragment rate; a legitimately healthy run
   finishes in well under that. */
#define CAN_RELAY_OVERALL_TIMEOUT_MS (20u*60u*1000u)

/* ── shared finish helpers ───────────────────────────────────────────────
   Called by handleGen2()/handleGen3() (never by handleDetect(), which has
   its own failure exits, or by handler()'s overall-timeout check, which
   calls finishFailed() directly) — the only two ways a relay ever ends.
   Deliberately asymmetric: finishSuccess() sends "switch back to app"
   first; finishFailed() does NOT (known, deliberately-unfixed gap — a
   device that failed mid-relay, after already being switched into the
   bootloader, is left stranded there; recovery today is just power-
   cycling it — see [[can_relay_gen3_bootloader]] memory for the full
   rationale, this was a deliberate accepted tradeoff, not an oversight). */
void CanRelay::finishSuccess(void) {
    phase = RELAY_PHASE_RETURNING;
    can.SendMessage(canId(1, 123, 0), 0,22,1, 0xFF,0xFF,0xFF,0xFF,0xFF);
    logRelayInfo("switch-to-app sent");
    status = RELAY_DONE;
    logRelayInfo("result=DONE");
}
void CanRelay::finishFailed(void) {
    failed = true;
    status = RELAY_ERROR;
    logRelayInfo("result=ERROR");
}

/* ── detection ────────────────────────────────────────────────────────────
   Switches the target into its bootloader and identifies which flashing
   protocol generation it speaks, before handing off to handleGen2()/
   handleGen3(). Own local step, separate from either generation's —
   never collides since only one of these three functions is ever the
   "active" one per relay (handler() dispatches by phase, see below). */
void CanRelay::handleDetect(void) {
    static uint32_t t = 0;
    static uint32_t phaseStart = 0;

    switch (detectStep) {
    case 0:
        /* A previous failed attempt can leave the target stranded in the
           bootloader (see finishFailed()'s own comment). If that's the
           case here, sending "switch app into bootloader" addressed to
           targetType goes completely unanswered: once actually in the
           bootloader, the device only listens as type 123 (see
           HCU-Bootloader's messages.cpp — RecieverType must equal its own
           can.idType, which becomes 123 there), not its normal app type.
           Check first with the same type-123-addressed version query
           case 1 polls with below — one CAN round-trip, and if it answers
           we skip the pointless switch send entirely (and the up-to-15s
           wait that would otherwise still succeed only because case 1's
           own periodic re-poll happens to also be addressed to type 123). */
        bootloaderSeen = false;
        can.SendMessage(canId(6, 123, 0), 0,18, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        t = core.getTick();
        detectStep = -1;
        logRelayInfo("checking whether target is already in bootloader...");
        break;
    case -1:
        if (bootloaderSeen) {
            logRelayInfo("already in bootloader, skipping switch command");
            phaseStart = core.getTick();
            detectStep = 1;
            break;
        }
        if ((core.getTick() - t) >= 500) {
            can.SendMessage(canId(1, targetType, targetAddress), 0,22,0, 0xFF,0xFF,0xFF,0xFF,0xFF);
            t = core.getTick();
            phaseStart = t;
            detectStep = 1;
            logRelayInfo("switch-to-bootloader sent, waiting for type 123...");
        }
        break;
    case 1:
        if (bootloaderSeen) {
            /* Which flashing protocol generation this specific bootloader
               actually supports safely — not every one out there behaves
               the same, some are known unstable and must never be used for
               OTA (see Modem::lookupBootloaderAlgorithm()/BUILTIN_BOOTLOADERS,
               a compiled-in table only — no runtime fetch of any kind, see
               its own comment in Modem.cpp). 2 = gen2 (handleGen2()); 3 =
               gen3 (handleGen3()); anything else (0 = version not in the
               table, 1 = known unstable) is refused outright rather than
               attempting it and hoping. */
            uint8_t algo = modem.lookupBootloaderAlgorithm(bootloaderVersion);
            if (algo != 2 && algo != 3) {
                /* algo==0 means this exact bootloader version isn't in
                   BUILTIN_BOOTLOADERS — since that table is the only source
                   now (no server fetch to fall back on), the actual fix is
                   a modem firmware update that adds this version, not a
                   retry or a Load. */
                const char* reason = (algo == 0) ? "bootloader-unknown-modem-firmware-update-needed" : "bootloader-algorithm-unsafe";
                logRelayFail(1, reason, algo, false);
                finishFailed();
                break;
            }
            algorithm = algo;
            /* Safe protocol doesn't mean safe IMAGE — this specific
               bootloader build is meant for one hardware family (see
               BootloaderEntry's own comment in Modem.h: 123.0.2.x is
               MBC-2/type-125 only, 123.0.0.x is the Binar family, 123.0.3.x
               is control panels). If the operator asked to relay onto a
               targetType this bootloader wasn't built for, refuse — the
               staged image's flash layout/expectations may not match this
               hardware at all, a real bricking risk distinct from the
               algorithm question above. */
            if (!modem.isDeviceTypeSupportedByBootloader(bootloaderVersion, targetType)) {
                logRelayFail(1, "bootloader-device-type-mismatch", targetType, false);
                finishFailed();
                break;
            }
            phase = RELAY_PHASE_DETECTED;
            logRelayInfoNum("bootloader detected, algorithm", algorithm);
            break;
        }
        if ((core.getTick() - phaseStart) >= 15000) { logRelayFail(1, "bootloader-timeout", 0, false); finishFailed(); break; }
        if ((core.getTick() - t) >= 800) {
            can.SendMessage(canId(6, 123, 0), 0,18, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
            t = core.getTick();
        }
        break;
    }
}

/* ── gen2: PGN=105 (control)/106 (raw fragment bytes) ────────────────────
   0   erase the bootloader's whole program region in one shot (sub6,
       D[1]=255 — "erase all", same command CAN-Tool always uses). This
       also wipes Config/BB_Common (sectors 2-4), a known, accepted
       tradeoff (see the sector-map memory note) rather than something
       this relay tries to prevent — confirmed in the real bootloader
       source, messages.cpp: 255 always wipes sectors 2-7, not just
       wherever the app lives.
   10-17 per-fragment loop, one CAN_RELAY_FRAGMENT_SIZE (512 byte) fragment
       at a time: set the bootloader's write address (sub0/1), stream the
       fragment as raw 8-byte PGN=106 frames (one per tick — never burst
       multiple sends in one call; see the SendMessage() call sites
       elsewhere in this file and StringTransfer.cpp for why: the CAN
       peripheral has a handful of TX mailboxes and SendMessage() doesn't
       block or retry, so blasting frames in a tight loop would silently
       drop the tail once mailboxes fill), verify what the bootloader
       actually received via a length+checksum query (sub2/3 — the
       checksum, crc += byte*170771; crc ^= (crc>>16)&0xFFFF, is confirmed
       from the real PC tool's C# source, not reconstructed), and only
       then commit it from the bootloader's RAM into its own flash
       (sub4/5). A verify mismatch or missing response at any point
       retries the whole fragment (re-send address included) rather than
       just the failed piece, up to CAN_RELAY_MAX_RETRIES times.
   On all fragments done: finishSuccess(). On any unretryable failure:
   finishFailed(). phase is set at each transition (see Phase's own
   comment in CanRelay.h) purely for the web UI. */
void CanRelay::handleGen2(void) {
    static int8_t   step = 0;
    static uint32_t t = 0;
    static uint16_t byteOffset = 0;     /* 0..CAN_RELAY_FRAGMENT_SIZE, within the current fragment */
    static uint32_t fragCrc = 0;
    static uint32_t lastFrameTick = 0;  /* paces the PGN=106 burst below — see its comment */

    if (phase == RELAY_PHASE_DETECTED) { step = 0; phase = RELAY_PHASE_ERASING; } /* fresh entry */

    switch (step) {
    case 0:
        can.SendMessage(canId(105, 123, 0), 6, 255, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        eraseGotResp = false;
        t = core.getTick();
        retries = 0;
        step = 1;
        logRelayInfo("erase-all sent");
        break;
    case 1:
        if (eraseGotResp) {
            if (eraseResult != 0) {
                logRelayFail(1, "erase-result", eraseResult, false);
                totalFails++;
                if (++retries >= 3) { finishFailed(); break; }
                step = 0;
                break;
            }
            logRelayInfo("erase ok");
            phase = RELAY_PHASE_ERASED; step = 10;
        } else if ((core.getTick() - t) >= 8000) {
            logRelayFail(1, "erase-timeout-retries", retries, false);
            if (++retries >= 3) { finishFailed(); break; }
            step = 0;
        }
        break;

    case 10:
        if (fragment >= fragmentTotal) { logRelayInfo("all fragments done"); finishSuccess(); break; }
        phase = RELAY_PHASE_TRANSFERRING;
        retries = 0;
        step = 11;
        break;
    case 11: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE;
        can.SendMessage(canId(105, 123, 0), 0,
            (uint8_t)(addr>>24), (uint8_t)(addr>>16), (uint8_t)(addr>>8), (uint8_t)addr,
            0xFF,0xFF,0xFF);
        setAddrGotResp = false;
        t = core.getTick();
        step = 12;
        break;
    }
    case 12: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE;
        if (setAddrGotResp) {
            if (setAddrEcho != addr) {
                logRelayFail(12, "setaddr-echo-mismatch(want)", addr, true);
                logRelayFail(12, "setaddr-echo-mismatch(got)", setAddrEcho, true);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            byteOffset = 0;
            fragCrc = 0;
            step = 13;
        } else if ((core.getTick() - t) >= 500) {
            logRelayFail(12, "setaddr-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    }
    case 13: {
        /* Paced ~3ms apart, not fired every tick — confirmed on real
           hardware that back-to-back sends with no gap easily outrun what
           a 250 kbit/s bus can actually drain, the hardware only has 3 TX
           mailboxes, and SendMessage() silently drops a frame instead of
           queuing/blocking once all 3 are still busy (see Can::txReady(),
           kept as a belt-and-suspenders check alongside the delay, not
           instead of it). 2ms plus the mailbox check alone still lost
           roughly 1 frame per fragment fairly often — bumped to 3ms and,
           more importantly, Work_C::handler() now holds off the modem's
           own other periodic CAN traffic (canBroadcast()/stringTransfer)
           for the whole relay so it isn't competing for the same 3
           mailboxes. */
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        const uint8_t* src = (const uint8_t*)(FLASH_OTA_BUF_ADDR
            + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE + byteOffset);
        for (uint8_t i = 0; i < 8; i++) {
            fragCrc += (uint32_t)src[i] * 170771U;
            fragCrc ^= (fragCrc >> 16) & 0xFFFFU;
        }
        can.SendMessage(canId(106, 123, 0), src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
        lastFrameTick = core.getTick();
        byteOffset += 8;
        if (byteOffset >= CAN_RELAY_FRAGMENT_SIZE) step = 14;
        break;
    }
    case 14:
        /* Same ~3ms gap after the last PGN=106 frame before sub2 —
           mailboxes could still be draining right after the burst above. */
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        can.SendMessage(canId(105, 123, 0), 2, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        checkGotResp = false;
        t = core.getTick();
        step = 15;
        break;
    case 15:
        if (checkGotResp) {
            if (checkLen != CAN_RELAY_FRAGMENT_SIZE || checkCrc != fragCrc) {
                logRelayFail(15, "verify-len(got)", checkLen, false);
                logRelayFail(15, "verify-crc(want)", fragCrc, true);
                logRelayFail(15, "verify-crc(got)", checkCrc, true);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            step = 16;
        } else if ((core.getTick() - t) >= 800) {
            logRelayFail(15, "verify-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    case 16:
        can.SendMessage(canId(105, 123, 0), 4, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        flashGotResp = false;
        t = core.getTick();
        step = 17;
        break;
    case 17:
        if (flashGotResp) {
            if (flashResult != 0) {
                logRelayFail(17, "flash-result(code)", flashResult, false);
                logRelayFail(17, "flash-result-frag", fragment, false);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            fragment++;
            if ((fragment % 16) == 0 || fragment == fragmentTotal)
                logRelayInfoNum("fragment ok, done", fragment);
            step = 10;
        } else if ((core.getTick() - t) >= 2000) {
            logRelayFail(17, "flash-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    }
}

/* ── gen3: PGN=110 (control)/111 (raw fragment bytes) ────────────────────
   Same shape and step numbering as handleGen2() above (deliberately, for
   easy side-by-side comparison) — the only real differences are: the
   erase sub-command (mode byte instead of a sector number/255), the PGN
   numbers, and a real CRC32 in place of the custom checksum.

   0   erase-memory sub-command (sub6), D[1]=1 = "erase main program
       only" — the bootloader itself knows which physical sectors that
       means; no sector list needed from us at all. Actually safer than
       gen2's erase, since it doesn't touch Config/BB_Common (the
       protocol doc lists other modes too — 0=full erase except
       bootloader, 2=by sector, 3=by address, 4=settings, 5/6=black-box
       general/errors — none of which this relay ever uses).
   10-17 per-fragment loop, identical shape to gen2's — see its own
       comment for the pacing/mailbox rationale, unchanged here.

   Magic word (protocol doc, added after this relay was first written):
   D[6..7] of both the erase (sub6, case 0) and write/flash-commit (sub4,
   case 16) command frames must carry 0xAA55 — big-endian, D[6]=0xAA,
   D[7]=0x55, matching this whole protocol's byte order (see
   ProcessCanMessage's setAddrEcho/checkCrc parsing above) — or the
   bootloader won't act on either command. Extra guard against an
   accidental erase/flash from a malformed or unrelated frame; the other
   sub-commands (0=set address, 2=check) don't require it. */
void CanRelay::handleGen3(void) {
    static int8_t   step = 0;
    static uint32_t t = 0;
    static uint16_t byteOffset = 0;
    static uint32_t fragCrc = 0;
    static uint32_t lastFrameTick = 0;

    if (phase == RELAY_PHASE_DETECTED) { step = 0; phase = RELAY_PHASE_ERASING; } /* fresh entry */

    switch (step) {
    case 0:
        can.SendMessage(canId(110, 123, 0), 6, 1, 0xFF,0xFF,0xFF,0xFF,0xAA,0x55);
        eraseGotResp = false;
        t = core.getTick();
        retries = 0;
        step = 1;
        logRelayInfo("erase-program-only sent");
        break;
    case 1:
        if (eraseGotResp) {
            if (eraseResult != 0) {
                logRelayFail(1, "erase-result", eraseResult, false);
                totalFails++;
                if (++retries >= 3) { finishFailed(); break; }
                step = 0;
                break;
            }
            logRelayInfo("erase ok");
            phase = RELAY_PHASE_ERASED; step = 10;
        } else if ((core.getTick() - t) >= 8000) {
            logRelayFail(1, "erase-timeout-retries", retries, false);
            if (++retries >= 3) { finishFailed(); break; }
            step = 0;
        }
        break;

    case 10:
        if (fragment >= fragmentTotal) { logRelayInfo("all fragments done"); finishSuccess(); break; }
        phase = RELAY_PHASE_TRANSFERRING;
        retries = 0;
        step = 11;
        break;
    case 11: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE;
        can.SendMessage(canId(110, 123, 0), 0,
            (uint8_t)(addr>>24), (uint8_t)(addr>>16), (uint8_t)(addr>>8), (uint8_t)addr,
            0xFF,0xFF,0xFF);
        setAddrGotResp = false;
        t = core.getTick();
        step = 12;
        break;
    }
    case 12: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE;
        if (setAddrGotResp) {
            if (setAddrEcho != addr) {
                logRelayFail(12, "setaddr-echo-mismatch(want)", addr, true);
                logRelayFail(12, "setaddr-echo-mismatch(got)", setAddrEcho, true);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            byteOffset = 0;
            fragCrc = 0xFFFFFFFFu; /* standard CRC-32 init — see case 13/15 */
            step = 13;
        } else if ((core.getTick() - t) >= 500) {
            logRelayFail(12, "setaddr-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    }
    case 13: {
        /* Same ~3ms pacing/mailbox rationale as handleGen2()'s case 13. */
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        const uint8_t* src = (const uint8_t*)(FLASH_OTA_BUF_ADDR
            + (uint32_t)fragment * CAN_RELAY_FRAGMENT_SIZE + byteOffset);
        /* Standard reflected CRC-32 (poly 0xEDB88320, per the protocol
           doc — the same algorithm zip/png/ethernet use), one byte at a
           time: XOR the byte in, then shift 8 times, XORing in the
           polynomial whenever the low bit is set. The doc only states the
           polynomial itself, not init/refin/refout/xorout explicitly —
           0xEDB88320 specifically being the *reflected* constant makes
           the standard bit-reflected form (init 0xFFFFFFFF, final XOR
           0xFFFFFFFF — applied at comparison time in case 15, not
           accumulated here) the only sensible reading, but this hasn't
           been verified against a real gen3 device yet. If gen3 verify
           starts failing consistently, this is the first thing to
           re-check against real hardware. */
        for (uint8_t i = 0; i < 8; i++) {
            fragCrc ^= src[i];
            for (uint8_t bit = 0; bit < 8; bit++)
                fragCrc = (fragCrc & 1) ? (fragCrc >> 1) ^ 0xEDB88320u : (fragCrc >> 1);
        }
        can.SendMessage(canId(111, 123, 0), src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
        lastFrameTick = core.getTick();
        byteOffset += 8;
        if (byteOffset >= CAN_RELAY_FRAGMENT_SIZE) step = 14;
        break;
    }
    case 14:
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        can.SendMessage(canId(110, 123, 0), 2, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        checkGotResp = false;
        t = core.getTick();
        step = 15;
        break;
    case 15:
        if (checkGotResp) {
            /* Final XOR applied here, not accumulated into fragCrc itself
               — so a retry of this same fragment (step goes back to 11)
               can re-run case 13 and keep accumulating the raw running
               register from a fresh 0xFFFFFFFF without needing to
               "un-XOR" first. */
            uint32_t wantCrc = fragCrc ^ 0xFFFFFFFFu;
            if (checkLen != CAN_RELAY_FRAGMENT_SIZE || checkCrc != wantCrc) {
                logRelayFail(15, "verify-len(got)", checkLen, false);
                logRelayFail(15, "verify-crc(want)", wantCrc, true);
                logRelayFail(15, "verify-crc(got)", checkCrc, true);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            step = 16;
        } else if ((core.getTick() - t) >= 800) {
            logRelayFail(15, "verify-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    case 16:
        can.SendMessage(canId(110, 123, 0), 4, 0xFF,0xFF,0xFF,0xFF,0xFF,0xAA,0x55);
        flashGotResp = false;
        t = core.getTick();
        step = 17;
        break;
    case 17:
        if (flashGotResp) {
            if (flashResult != 0) {
                logRelayFail(17, "flash-result(code)", flashResult, false);
                logRelayFail(17, "flash-result-frag", fragment, false);
                totalFails++;
                if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
                step = 11;
                break;
            }
            fragment++;
            if ((fragment % 16) == 0 || fragment == fragmentTotal)
                logRelayInfoNum("fragment ok, done", fragment);
            step = 10;
        } else if ((core.getTick() - t) >= 2000) {
            logRelayFail(17, "flash-timeout-frag", fragment, false);
            if (++retries >= CAN_RELAY_MAX_RETRIES) { finishFailed(); break; }
            step = 11;
        }
        break;
    }
}

/* ── top-level dispatcher ─────────────────────────────────────────────── */
void CanRelay::handler(void) {
    static uint32_t overallStart = 0;

    if (status != RELAY_STAGING) {
        if (startRequested) {
            startRequested = false;
            if (!modem.ota.stagedValid || modem.ota.stagedBytes == 0
                || (modem.ota.stagedBytes % CAN_RELAY_FRAGMENT_SIZE) != 0) {
                status = RELAY_ERROR;
                return;
            }
            /* CRITICAL SAFETY BACKSTOP — refuse any flashBase inside the
               bootloader's own two sectors (0x08000000-0x08007FFF, 16 KB
               each — the app region always starts at 0x08008000 or later,
               see the sector map in [[ota_relay_sector_verify]]). No real
               published profile's flashBase is ever below that; the only
               way to reach it is a bug (flashBase left at its RAM-only
               default 0, an erased/corrupted meta page, a wrong profile).
               Worth checking even though modem.ota.flashBase is now
               persisted and normally survives a reboot fine (see
               Modem::refreshStagedInfo()/Flash_C::writeOtaMeta()) — this is
               a hard floor for whatever isn't covered by that.

               This isn't just belt-and-suspenders against our own bugs —
               read the actual bootloader source (HCU-Bootloader_123-0-2-7,
               messages.cpp PGN=105 handler) 2026-08-29: the "set fragment
               address" command (sub0) takes the target address straight
               from the CAN payload with ZERO range validation, and the
               "commit fragment" command (sub4) then calls
               FLASH_ProgramWord() at that address for however many bytes,
               also unchecked. Its own erase commands (both the legacy one
               and the sub6 D[1]=255 one gen2 uses) only ever erase sectors
               2-11 — there is no way to erase sectors 0/1 through this
               protocol at all. So a bad flashBase reaching the bootloader
               doesn't just risk a failed relay — the bootloader will
               program straight over its own un-erased vector table with no
               self-protection whatsoever (this is confirmed to be exactly
               what bricked a real device the same day: byte-compared the
               known-good bootloader against what a programmer read back
               off the bricked one — every differing byte in the vector
               table had bits only cleared, never set, the unmistakable
               signature of an unerased flash write). The bootloader isn't
               going to protect itself here, so we have to. */
            if (modem.ota.flashBase < 0x08008000u) {
                logRelayFail(0, "flash-base-in-bootloader-region-refused", modem.ota.flashBase, true);
                status = RELAY_ERROR;
                return;
            }
            status = RELAY_STAGING;
            phase = RELAY_PHASE_SWITCHING;
            detectStep = 0; /* handleDetect() has no other way to tell "fresh relay" from
                                "still mid-poll" — see detectStep's own comment in CanRelay.h */
            failed = false;
            retries = 0;
            totalFails = 0;
            fragment = 0;
            fragmentTotal = (uint16_t)(modem.ota.stagedBytes / CAN_RELAY_FRAGMENT_SIZE);
            overallStart = core.getTick();
            logRelayInfo("start");
            logRelayInfoNum("fragmentTotal", fragmentTotal);
            logRelayInfoNum("targetType", targetType);
            logRelayInfoNum("targetAddress", targetAddress);
        }
        return;
    }

    if ((core.getTick() - overallStart) >= CAN_RELAY_OVERALL_TIMEOUT_MS) {
        logRelayFail(0, "overall-timeout", (uint32_t)(core.getTick() - overallStart), false);
        finishFailed();
        return;
    }

    if (phase == RELAY_PHASE_SWITCHING) { handleDetect(); return; }
    if (algorithm == 3) handleGen3(); else handleGen2();
}
