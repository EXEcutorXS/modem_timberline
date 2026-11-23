#include "verify.h"
#include "pubkeys.h"

extern "C" {
#include "sha256.h"
#include "tweetnacl.h"
}

#include <string.h>

/* Message text must match host/tools/sign_firmware.py and
   host/timberline-web/firmwareSig.js byte for byte. */

static int appendStr(char* b, int n, int cap, const char* s) {
    while (*s && n < cap - 1) b[n++] = *s++;
    b[n] = 0;
    return n;
}

static int appendU(char* b, int n, int cap, uint32_t v) {
    char tmp[10];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    else while (v && t < 10) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t > 0 && n < cap - 1) b[n++] = tmp[--t];
    b[n] = 0;
    return n;
}

static int appendHex8(char* b, int n, int cap, uint32_t v) {
    /* A function-local pointer to a string is left as 0 in RAM by this
       compiler (the initializer never lands in RW). Reading through it
       pulls bytes from address 0, so the signed text no longer matches
       and every image fails as bad-signature. An array lives in flash. */
    static const char hexd[] = "0123456789abcdef";
    for (int i = 7; i >= 0 && n < cap - 1; i--)
        b[n++] = hexd[(v >> (i * 4)) & 0xF];
    b[n] = 0;
    return n;
}

static void hex32(const uint8_t h[32], char out[65]) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2]     = hexd[h[i] >> 4];
        out[i * 2 + 1] = hexd[h[i] & 0xF];
    }
    out[64] = 0;
}

static bool ed25519Ok(const uint8_t pk[32], const uint8_t sig[64], const char* msg) {
    const unsigned msgLen = (unsigned)strlen(msg);
    if (msgLen == 0 || msgLen > 180) return false;

    uint8_t sm[64 + 180];
    uint8_t opened[64 + 180];
    memcpy(sm, sig, 64);
    memcpy(sm + 64, msg, msgLen);

    uint64_t mlen = 0;
    return crypto_sign_open(opened, &mlen, sm, (uint64_t)64 + msgLen, pk) == 0;
}

bool firmwareSignatureOk(const char* version, uint32_t flashBase,
                         const uint8_t* image, uint32_t imageLen,
                         const uint8_t sig[64]) {
    if (!version || !version[0] || !image || imageLen == 0 || !sig) return false;

    SHA256_CTX ctx;
    uint8_t hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, image, imageLen);
    sha256_final(&ctx, hash);

    char hex[65];
    hex32(hash, hex);

    char msg[160];
    int n = 0;
    n = appendStr(msg, n, (int)sizeof(msg), "TLFW1|");
    n = appendStr(msg, n, (int)sizeof(msg), version);
    n = appendStr(msg, n, (int)sizeof(msg), "|");
    n = appendHex8(msg, n, (int)sizeof(msg), flashBase);
    n = appendStr(msg, n, (int)sizeof(msg), "|");
    n = appendU(msg, n, (int)sizeof(msg), imageLen);
    n = appendStr(msg, n, (int)sizeof(msg), "|");
    n = appendStr(msg, n, (int)sizeof(msg), hex);
    if (n >= (int)sizeof(msg) - 1) return false;

    return ed25519Ok(FIRMWARE_ED25519_PUBLIC, sig, msg);
}
