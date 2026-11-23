"""Rename published firmware bins to the signed filename form.

Unsigned files look like "<version>_0x<flashBase>.bin". The bytes stay
exactly as they are; only the Ed25519 signature is added to the name.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sign_firmware import sign_image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE_ROOT = os.path.join(REPO, "timberline-web", "public", "firmware")
UNSIGNED = re.compile(r"^(\d+(?:\.\d+){0,3})_0x([0-9A-Fa-f]+)\.bin$")
SIGNED = re.compile(r"^(\d+(?:\.\d+){0,3})_0x([0-9A-Fa-f]+)_([0-9A-Fa-f]{128})\.bin$")


def main():
    renamed = skipped = 0
    for dev_type in sorted(os.listdir(FIRMWARE_ROOT)):
        type_dir = os.path.join(FIRMWARE_ROOT, dev_type)
        if not os.path.isdir(type_dir):
            continue
        for name in sorted(os.listdir(type_dir)):
            path = os.path.join(type_dir, name)
            if not os.path.isfile(path):
                continue
            if SIGNED.match(name):
                skipped += 1
                continue
            m = UNSIGNED.match(name)
            if not m:
                print(f"skip (unexpected name): {path}", file=sys.stderr)
                continue
            version, base_hex = m.group(1), m.group(2)
            with open(path, "rb") as f:
                image = f.read()
            sig = sign_image(version, int(base_hex, 16), image)
            new_name = f"{version}_0x{base_hex}_{sig}.bin"
            new_path = os.path.join(type_dir, new_name)
            if os.path.exists(new_path):
                raise SystemExit(f"already exists: {new_path}")
            os.replace(path, new_path)
            print(f"{dev_type}/{name} -> {new_name}")
            renamed += 1
    print(f"renamed {renamed}, already signed {skipped}")


if __name__ == "__main__":
    main()
