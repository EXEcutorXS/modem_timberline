#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
flash_report.py — Post-build flash/RAM usage report for the modem
(modemDragonfly), ported from PU28-Timberline/tools/flash_report.py.

Keil's own "Program Size" line and the .map file's summary split usage
into 3 separate numbers (Total RO / Total RW / Total ROM Size) and report
against the whole chip's flash (512 KB) — not what's actually available to
the application after the bootloader's reserved region below it AND the
OTA staging buffer (for relaying other devices' firmware over CAN, e.g.
ПУ28-Timberline/MBC-2 — see Library/Flash/flash.h) reserved above it. This
prints ONE clear number instead: the real flash footprint (Code + RO Data
+ RW Data — "Total ROM Size" is already exactly this) against the app's
REAL budget (IROM1, modemDragonfly.uvprojx's OCR_RVCT4), plus RAM the
same way (OCR_RVCT9).

Run automatically after each build via Keil's After-Build user command
(modemDragonfly.uvprojx's <AfterMake>, chained after __Get_OTA_Info.exe by
after_build.bat — see that file):
    python tools\\flash_report.py

Or run manually any time after building from the IDE — reads the
already-generated Listings\\modemDragonfly.map, doesn't invoke the
compiler itself.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UVPROJX = ROOT / "modemDragonfly.uvprojx"
MAP_FILE = ROOT / "Listings" / "modemDragonfly.map"


def read_ocr_region(name: str, xml: str):
    """Extract (start, size) from modemDragonfly.uvprojx's <OCR_RVCTn>
    block — single source of truth for the app's real flash/RAM budget, so
    this report can't silently drift from the actual linker scatter if
    that ever changes (e.g. the OTA buffer growing again, see flash.h)."""
    m = re.search(
        rf"<OCR_{name}>.*?<StartAddress>(0x[0-9A-Fa-f]+)</StartAddress>\s*"
        rf"<Size>(0x[0-9A-Fa-f]+)</Size>",
        xml, re.S)
    if not m:
        raise RuntimeError(f"OCR_{name} not found in {UVPROJX}")
    return int(m.group(1), 16), int(m.group(2), 16)


def read_map_totals(map_text: str):
    """Return (ro, rw_zi, rom) byte counts from the map file's own summary."""
    def grab(label):
        m = re.search(rf"Total {label} Size.*?\)\s+(\d+)", map_text, re.S)
        if not m:
            raise RuntimeError(f"'{label.strip()}' total not found in {MAP_FILE}")
        return int(m.group(1))
    return grab("RO "), grab("RW "), grab("ROM")


def bar(used, total, width=30):
    filled = max(0, min(width, int(width * used / total))) if total else 0
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def main():
    if not MAP_FILE.exists():
        print(f"flash_report: {MAP_FILE} not found — build first", file=sys.stderr)
        return 1

    xml = UVPROJX.read_text(encoding="utf-8")
    rom_start, rom_size = read_ocr_region("RVCT4", xml)   # IROM1: app code region
    ram_start, ram_size = read_ocr_region("RVCT9", xml)   # app RAM region

    map_text = MAP_FILE.read_text(encoding="utf-8", errors="replace")
    _ro, rw_zi, rom_used = read_map_totals(map_text)
    ram_used = rw_zi   # RW Data + ZI Data = everything actually living in RAM

    rom_free = rom_size - rom_used
    ram_free = ram_size - ram_used

    print("=" * 62)
    print(f"FLASH  app region 0x{rom_start:08X}, {rom_size / 1024:.0f} KB "
          f"(bootloader reserves 0x{rom_start - 0x08000000:X} bytes below it; "
          f"OTA staging buffer + serial/config reserved above it — see "
          f"Library/Flash/flash.h)")
    print(f"  used {rom_used:7d} B  ({rom_used / 1024:6.1f} KB)  {rom_used / rom_size * 100:5.1f}%   "
          f"free {rom_free:7d} B  ({rom_free / 1024:6.1f} KB)")
    print(f"  {bar(rom_used, rom_size)}")
    print()
    print(f"RAM    0x{ram_start:08X}, {ram_size / 1024:.0f} KB")
    print(f"  used {ram_used:7d} B  ({ram_used / 1024:6.1f} KB)  {ram_used / ram_size * 100:5.1f}%   "
          f"free {ram_free:7d} B  ({ram_free / 1024:6.1f} KB)")
    print(f"  {bar(ram_used, ram_size)}")
    print("=" * 62)

    if rom_used > rom_size:
        print("!! FLASH OVERFLOW — image does not fit in the app's flash region !!")
        return 1
    if ram_used > ram_size:
        print("!! RAM OVERFLOW !!")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
