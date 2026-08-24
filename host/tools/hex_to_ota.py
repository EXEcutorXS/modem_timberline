#!/usr/bin/env python3
"""Converts a Keil-produced Intel HEX firmware image (e.g. "43.2.6.13_STM_Main.hex")
into the firmware.bin + firmware.crc32 pair the web app's OTA pipeline expects
under host/timberline-web/public/firmware/<type>/<version>/ — see server.js's
/firmware/:type/:version/firmware.bin and /firmware/:type/versions routes, and
Modem::doOta() on the modem side which downloads+verifies page by page.

firmware.crc32 format/algorithm matches host/tools/make_firmware_crc.js exactly
(one little-endian uint32 CRC32 per 2048-byte page, standard zlib/PKZIP poly
0xEDB88320, same as Flash_C::crc32OtaPage() on the modem) — this script just
also does the .hex -> .bin step make_firmware_crc.js doesn't, in one pass.

Version (and therefore the destination folder) is read from the hex filename
itself: "<type>.<v2>.<v3>.<v4>_..." — matching how firmware is actually named
on disk already (e.g. "43.2.6.13_STM_Main.hex" -> type 43, version 43.2.6.13).
Pass --type/--version explicitly to override if a file isn't named that way.

The image's start address defaults to the lowest address present in the hex
file itself (this is how the existing 125.0.0.15 image was built — the linker
already places the app at its real flash base, e.g. 0x08020000, so the hex
file's own addresses are already correct and don't need a separate --base).
Gaps between records are filled with 0xFF (erased-flash value), and the whole
image is padded up to a multiple of 2048 bytes the same way
make_firmware_crc.js does.

This script only produces firmware.bin/firmware.crc32 — it does NOT touch
profile.txt (flashBase/eraseSectors, used by the modem's CAN relay step, see
Timberline::doCanRelay()). If firmware/<type>/profile.txt doesn't exist yet
for this device type, create it by hand once (flashBase should equal this
script's own printed "image starts at" address).

Usage:
    python hex_to_ota.py <firmware.hex> [<firmware2.hex> ...]
    python hex_to_ota.py --type 43 --version 43.2.6.13 43.2.6.13_STM_Main.hex
"""
import argparse
import os
import re
import sys

PAGE_SIZE = 2048
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # host/
FIRMWARE_ROOT = os.path.join(REPO_ROOT, 'timberline-web', 'public', 'firmware')


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return (~crc) & 0xFFFFFFFF


def parse_intel_hex(path: str) -> bytes:
    """Returns a flat bytes image starting at the lowest address in the file,
    gaps filled with 0xFF. Supports record types 00/01/02/04 (data, EOF,
    extended segment address, extended linear address) — the ones a Keil/ARMCC
    toolchain actually emits; anything else is ignored, matching how a
    straightforward flasher would treat them."""
    records = []  # (absolute_addr, data_bytes)
    upper = 0  # from type 04/02, combined with each record's 16-bit address

    with open(path, 'r') as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if line[0] != ':':
                raise ValueError(f'{path}:{lineno}: not an Intel HEX record: {line!r}')
            raw = bytes.fromhex(line[1:])
            byte_count, addr_hi, addr_lo, rec_type = raw[0], raw[1], raw[2], raw[3]
            data = raw[4:4 + byte_count]
            checksum = raw[4 + byte_count]
            if (sum(raw[:4 + byte_count]) + checksum) & 0xFF != 0:
                raise ValueError(f'{path}:{lineno}: checksum mismatch')

            if rec_type == 0x00:  # data
                addr16 = (addr_hi << 8) | addr_lo
                records.append((upper + addr16, data))
            elif rec_type == 0x01:  # EOF
                break
            elif rec_type == 0x02:  # extended segment address
                seg = (data[0] << 8) | data[1]
                upper = seg << 4
            elif rec_type == 0x04:  # extended linear address
                upper = ((data[0] << 8) | data[1]) << 16
            # other record types (03 start segment addr, 05 start linear addr) carry
            # no flash content — ignored on purpose.

    if not records:
        raise ValueError(f'{path}: no data records found')

    base = min(addr for addr, _ in records)
    end = max(addr + len(data) for addr, data in records)
    image = bytearray(b'\xFF' * (end - base))
    for addr, data in records:
        image[addr - base:addr - base + len(data)] = data
    return base, bytes(image)


def build_one(hex_path: str, dev_type: str, version: str):
    base, image = parse_intel_hex(hex_path)

    pad = (PAGE_SIZE - (len(image) % PAGE_SIZE)) % PAGE_SIZE
    if pad:
        image += b'\xFF' * pad

    page_count = len(image) // PAGE_SIZE
    crc_bytes = bytearray(page_count * 4)
    for page in range(page_count):
        page_crc = crc32(image[page * PAGE_SIZE:(page + 1) * PAGE_SIZE])
        crc_bytes[page * 4:page * 4 + 4] = page_crc.to_bytes(4, 'little')

    out_dir = os.path.join(FIRMWARE_ROOT, dev_type, version)
    os.makedirs(out_dir, exist_ok=True)
    bin_path = os.path.join(out_dir, 'firmware.bin')
    crc_path = os.path.join(out_dir, 'firmware.crc32')
    with open(bin_path, 'wb') as f:
        f.write(image)
    with open(crc_path, 'wb') as f:
        f.write(crc_bytes)

    print(f'{hex_path}')
    print(f'  image starts at 0x{base:08X}, {len(image)} bytes, {page_count} pages')
    print(f'  -> {bin_path}')
    print(f'  -> {crc_path}')

    profile_path = os.path.join(FIRMWARE_ROOT, dev_type, 'profile.txt')
    if not os.path.exists(profile_path):
        print(f'  NOTE: {profile_path} does not exist yet — the CAN relay step '
              f'(flashing this onto a real device) needs it. flashBase should be '
              f'0x{base:08X}; eraseSectors depends on this device\'s own flash '
              f'layout, fill in by hand.')


FILENAME_VERSION_RE = re.compile(r'^(\d+)\.(\d+)\.(\d+)\.(\d+)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('hex_files', nargs='+')
    ap.add_argument('--type', help='device type (overrides parsing it from the filename)')
    ap.add_argument('--version', help='full version, e.g. 43.2.6.13 (overrides parsing it from the filename; only valid with a single hex file)')
    args = ap.parse_args()

    if args.version and len(args.hex_files) != 1:
        ap.error('--version only makes sense with a single input file')

    for hex_path in args.hex_files:
        if args.version:
            version = args.version
            dev_type = args.type or version.split('.')[0]
        else:
            m = FILENAME_VERSION_RE.match(os.path.basename(hex_path))
            if not m:
                print(f'{hex_path}: filename does not start with "<type>.<v2>.<v3>.<v4>" — '
                      f'pass --type/--version explicitly', file=sys.stderr)
                sys.exit(1)
            version = '.'.join(m.groups())
            dev_type = args.type or m.group(1)
        build_one(hex_path, dev_type, version)


if __name__ == '__main__':
    main()
