#!/usr/bin/env python3
"""Converts a Keil-produced Intel HEX firmware image (e.g. "43.2.6.13_STM_Main.hex")
into the single self-describing .bin file the web app's OTA pipeline now expects
under host/timberline-web/public/firmware/<type>/ — see server.js's
/firmware/:type/:version/{firmware.bin,firmware.crc16,profile} routes and
Modem::doOta() on the modem side which downloads+verifies page by page.

Filename: "<version>_0x<flashBase>[_<sectors>].bin" — no per-version
subfolder, no profile.txt, no crc sidecar; everything else the modem needs
(flash base address, which sectors to erase, per-page CRC16) is derived by
the server from this one file's name and bytes. See host/README.md for the
full convention.

Version (and therefore the device-type folder) is read from the hex
filename itself: "<type>.<v2>.<v3>.<v4>_..." — matching how firmware is
already named on disk (e.g. "43.2.6.13_STM_Main.hex" -> type 43, version
43.2.6.13). Pass --type/--version explicitly to override if a file isn't
named that way.

flashBase defaults to the lowest address present in the hex file itself
(the linker already places the app at its real flash base, e.g.
0x08020000, so the hex file's own addresses are already correct and don't
need a separate --base). Gaps between records are filled with 0xFF
(erased-flash value); the image is NOT padded to a page boundary anymore —
the server pads the tail with 0xFF on the fly when a page is requested, so
what lands on disk here is exactly the hex file's own content.

--sectors is optional and is *not* something this script can infer — which
sectors are safe to erase without touching settings/black-box data is a
per-device/per-bootloader safety decision, not something derivable from the
hex file. Pass it explicitly (e.g. --sectors 5-6 or --sectors 2,5-15,
comma-separated single numbers and/or inclusive ranges); omitting it
publishes a file with no sector list at all, which the modem's CAN relay
then erases via the *whole program region* command instead of an explicit
list (see Timberline::doCanRelay()) — only appropriate once you've actually
confirmed that broad erase is safe for this specific device/bootloader.

Usage:
    python hex_to_ota.py --sectors 5-6 <firmware.hex> [<firmware2.hex> ...]
    python hex_to_ota.py --type 43 --version 43.2.6.13 --sectors 5,6 43.2.6.13_STM_Main.hex
"""
import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # host/
FIRMWARE_ROOT = os.path.join(REPO_ROOT, 'timberline-web', 'public', 'firmware')

SECTOR_SPEC_RE = re.compile(r'^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$')


def parse_intel_hex(path: str) -> bytes:
    """Returns (base_address, flat bytes image starting at that address),
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


def build_one(hex_path: str, dev_type: str, version: str, sectors: str):
    base, image = parse_intel_hex(hex_path)

    name = f'{version}_0x{base:08X}'
    if sectors:
        name += f'_{sectors}'
    name += '.bin'

    out_dir = os.path.join(FIRMWARE_ROOT, dev_type)
    os.makedirs(out_dir, exist_ok=True)
    bin_path = os.path.join(out_dir, name)
    with open(bin_path, 'wb') as f:
        f.write(image)

    print(f'{hex_path}')
    print(f'  image starts at 0x{base:08X}, {len(image)} bytes')
    print(f'  -> {bin_path}')
    if not sectors:
        print(f'  NOTE: no --sectors given — this publishes with no erase-sector list at '
              f'all, meaning the modem will erase the *whole program region* in one shot '
              f'when relaying it (see Timberline::doCanRelay()). Only fine if you\'ve '
              f'actually confirmed that\'s safe for this device/bootloader; otherwise '
              f're-run with --sectors.')


FILENAME_VERSION_RE = re.compile(r'^(\d+)\.(\d+)\.(\d+)\.(\d+)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('hex_files', nargs='+')
    ap.add_argument('--type', help='device type (overrides parsing it from the filename)')
    ap.add_argument('--version', help='full version, e.g. 43.2.6.13 (overrides parsing it from the filename; only valid with a single hex file)')
    ap.add_argument('--sectors', help='erase-sector spec, e.g. "5-6" or "2,5-15" — see the module docstring; omit to publish with no explicit list')
    args = ap.parse_args()

    if args.version and len(args.hex_files) != 1:
        ap.error('--version only makes sense with a single input file')
    if args.sectors and not SECTOR_SPEC_RE.match(args.sectors):
        ap.error(f'--sectors {args.sectors!r} does not look like "5-6" or "2,5-15"')

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
        build_one(hex_path, dev_type, version, args.sectors)


if __name__ == '__main__':
    main()
