#ifndef VERIFY_H
#define VERIFY_H

#include <stdint.h>

/* Ed25519 over the canonical firmware message
   TLFW1|<version>|<flashBase 8 lowercase hex>|<length>|<sha256 hex>.
   image points at the staged bytes in flash; imageLen is the raw file
   length (not the 0xFF-padded page span). */
bool firmwareSignatureOk(const char* version, uint32_t flashBase,
                         const uint8_t* image, uint32_t imageLen,
                         const uint8_t sig[64]);

#endif
