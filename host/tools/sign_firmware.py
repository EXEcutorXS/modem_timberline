"""Sign a firmware image with the offline Ed25519 key.

The signature covers
  TLFW1|<version>|<flash base, 8 lowercase hex>|<byte length>|<sha256 hex>
and is the 128 hex characters embedded in the published filename. The same
string is rebuilt by firmwareSig.js and by Library/Crypto/verify.cpp.
"""
import hashlib
from pathlib import Path

from cryptography.hazmat.primitives import serialization

REPO = Path(__file__).resolve().parents[2]
KEY_PATH = REPO / "host" / "tools" / "keys" / "firmware-ed25519.pem"


def firmware_message(version: str, flash_base: int, length: int, sha256_hex: str) -> bytes:
    base = f"{flash_base & 0xFFFFFFFF:08x}"
    return f"TLFW1|{version}|{base}|{length}|{sha256_hex}".encode("ascii")


def sign_image(version: str, flash_base: int, image: bytes, key_path=None) -> str:
    path = Path(key_path) if key_path else KEY_PATH
    if not path.is_file():
        raise SystemExit(
            f"missing {path} — run host/tools/gen_keys.py on the production machine"
        )
    priv = serialization.load_pem_private_key(path.read_bytes(), password=None)
    digest = hashlib.sha256(image).hexdigest()
    return priv.sign(firmware_message(version, flash_base, len(image), digest)).hex()
