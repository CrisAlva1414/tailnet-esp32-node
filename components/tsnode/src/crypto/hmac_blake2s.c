/*
 * HMAC-BLAKE2s / HKDF-BLAKE2s — implementation notes in hmac_blake2s.h.
 */

#include "hmac_blake2s.h"

#include <string.h>

#include "blake2s.h"

#define HMAC_BLOCK_LEN 64
#define HMAC_KEY_MAX 32 /* BLAKE2s key size; longer keys are hashed first */

static void hmac_init_pads(uint8_t ipad[HMAC_BLOCK_LEN],
                           uint8_t opad[HMAC_BLOCK_LEN],
                           const uint8_t *key, size_t keylen)
{
    uint8_t k[HMAC_KEY_MAX];
    memset(k, 0, sizeof(k));
    if (keylen > HMAC_KEY_MAX) {
        tsnode_blake2s(k, HMAC_KEY_MAX, key, keylen);
    } else {
        memcpy(k, key, keylen);
    }

    for (size_t i = 0; i < HMAC_KEY_MAX; i++) {
        ipad[i] = (uint8_t)(0x36u ^ k[i]);
        opad[i] = (uint8_t)(0x5cu ^ k[i]);
    }
    memset(ipad + HMAC_KEY_MAX, 0x36, HMAC_BLOCK_LEN - HMAC_KEY_MAX);
    memset(opad + HMAC_KEY_MAX, 0x5c, HMAC_BLOCK_LEN - HMAC_KEY_MAX);

    memset(k, 0, sizeof(k));
}

/* Inner hash H(ipad || a || b) with optional second part. */
static void hmac_inner(uint8_t inner[32], const uint8_t ipad[64],
                       const uint8_t *a, size_t a_len, const uint8_t *b,
                       size_t b_len)
{
    tsnode_blake2s_ctx ctx;
    tsnode_blake2s_init(&ctx, 32);
    tsnode_blake2s_update(&ctx, ipad, HMAC_BLOCK_LEN);
    if (a != NULL && a_len > 0) {
        tsnode_blake2s_update(&ctx, a, a_len);
    }
    if (b != NULL && b_len > 0) {
        tsnode_blake2s_update(&ctx, b, b_len);
    }
    tsnode_blake2s_final(&ctx, inner, 32);
}

static void hmac_outer(uint8_t out[32], const uint8_t opad[64],
                       const uint8_t inner[32])
{
    tsnode_blake2s_ctx ctx;
    tsnode_blake2s_init(&ctx, 32);
    tsnode_blake2s_update(&ctx, opad, HMAC_BLOCK_LEN);
    tsnode_blake2s_update(&ctx, inner, 32);
    tsnode_blake2s_final(&ctx, out, 32);
}

void tsnode_hmac_blake2s_1(uint8_t out[32], const uint8_t *key,
                           size_t keylen, const uint8_t *a, size_t a_len)
{
    uint8_t ipad[HMAC_BLOCK_LEN], opad[HMAC_BLOCK_LEN], inner[32];

    hmac_init_pads(ipad, opad, key, keylen);
    hmac_inner(inner, ipad, a, a_len, NULL, 0);
    hmac_outer(out, opad, inner);

    memset(ipad, 0, sizeof(ipad));
    memset(opad, 0, sizeof(opad));
    memset(inner, 0, sizeof(inner));
}

void tsnode_hmac_blake2s_2(uint8_t out[32], const uint8_t *key,
                           size_t keylen, const uint8_t *a, size_t a_len,
                           const uint8_t *b, size_t b_len)
{
    uint8_t ipad[HMAC_BLOCK_LEN], opad[HMAC_BLOCK_LEN], inner[32];

    hmac_init_pads(ipad, opad, key, keylen);
    hmac_inner(inner, ipad, a, a_len, b, b_len);
    hmac_outer(out, opad, inner);

    memset(ipad, 0, sizeof(ipad));
    memset(opad, 0, sizeof(opad));
    memset(inner, 0, sizeof(inner));
}

void tsnode_hkdf_blake2s_1(uint8_t t0[32], const uint8_t key[32],
                           const uint8_t *ikm, size_t ikm_len)
{
    uint8_t prk[32];

    tsnode_hmac_blake2s_1(prk, key, 32, ikm, ikm == NULL ? 0 : ikm_len);
    tsnode_hmac_blake2s_1(t0, prk, 32, (const uint8_t *)"\x01", 1);

    memset(prk, 0, sizeof(prk));
}

void tsnode_hkdf_blake2s_2(uint8_t t0[32], uint8_t t1[32],
                           const uint8_t key[32], const uint8_t *ikm,
                           size_t ikm_len)
{
    uint8_t prk[32];

    tsnode_hmac_blake2s_1(prk, key, 32, ikm, ikm == NULL ? 0 : ikm_len);
    tsnode_hmac_blake2s_1(t0, prk, 32, (const uint8_t *)"\x01", 1);
    tsnode_hmac_blake2s_2(t1, prk, 32, t0, 32, (const uint8_t *)"\x02", 1);

    memset(prk, 0, sizeof(prk));
}

void tsnode_hkdf_blake2s_3(uint8_t t0[32], uint8_t t1[32], uint8_t t2[32],
                           const uint8_t key[32], const uint8_t *ikm,
                           size_t ikm_len)
{
    uint8_t prk[32];

    tsnode_hmac_blake2s_1(prk, key, 32, ikm, ikm == NULL ? 0 : ikm_len);
    tsnode_hmac_blake2s_1(t0, prk, 32, (const uint8_t *)"\x01", 1);
    tsnode_hmac_blake2s_2(t1, prk, 32, t0, 32, (const uint8_t *)"\x02", 1);
    tsnode_hmac_blake2s_2(t2, prk, 32, t1, 32, (const uint8_t *)"\x03", 1);

    memset(prk, 0, sizeof(prk));
}
