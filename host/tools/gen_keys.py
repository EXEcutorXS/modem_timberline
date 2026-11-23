"""Create the offline Ed25519 keypair for firmware images.

Private keys land in host/tools/keys/ and are gitignored. Public keys are
written where the server and the firmware can see them. Re-running keeps
an existing private key and only refreshes the public copies.
"""
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

REPO = Path(__file__).resolve().parents[2]
PRIV_DIR = REPO / "host" / "tools" / "keys"
PUB_DIR = REPO / "host" / "timberline-web" / "keys"
CRYPTO_DIR = REPO / "Library" / "Crypto"


def load_or_create(name: str):
    PRIV_DIR.mkdir(parents=True, exist_ok=True)
    PUB_DIR.mkdir(parents=True, exist_ok=True)
    priv_path = PRIV_DIR / f"{name}-ed25519.pem"
    pub_path = PUB_DIR / f"{name}-ed25519.pub.pem"
    if priv_path.exists():
        priv = serialization.load_pem_private_key(priv_path.read_bytes(), password=None)
    else:
        priv = Ed25519PrivateKey.generate()
        priv_path.write_bytes(priv.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ))
    pub_path.write_bytes(priv.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    ))
    raw = priv.public_key().public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw,
    )
    return raw


def c_array(symbol: str, raw: bytes) -> str:
    body = ", ".join(f"0x{b:02x}" for b in raw)
    return f"const uint8_t {symbol}[32] = {{ {body} }};\n"


def main():
    firmware = load_or_create("firmware")
    (CRYPTO_DIR / "pubkeys.h").write_text(
        "#ifndef PUBKEYS_H\n"
        "#define PUBKEYS_H\n"
        "#include <stdint.h>\n"
        "extern const uint8_t FIRMWARE_ED25519_PUBLIC[32];\n"
        "#endif\n",
        encoding="utf-8",
        newline="\n",
    )
    (CRYPTO_DIR / "pubkeys.c").write_text(
        "#include <stdint.h>\n"
        "#include \"pubkeys.h\"\n"
        + c_array("FIRMWARE_ED25519_PUBLIC", firmware),
        encoding="utf-8",
        newline="\n",
    )
    print(f"public keys in {PUB_DIR}")
    print(f"private keys in {PRIV_DIR} (not for the server)")


if __name__ == "__main__":
    main()
