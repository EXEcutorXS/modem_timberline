#ifndef TWEETNACL_H
#define TWEETNACL_H

#include <stdint.h>

/* Public-domain TweetNaCl (tweetnacl.c). Verification only is used here. */
int crypto_sign_open(unsigned char *m, uint64_t *mlen,
                     const unsigned char *sm, uint64_t n,
                     const unsigned char *pk);

#endif
