/*
 * Host-test crypto backend for the WireGuard core vtable.
 *
 * - X25519: TweetNaCl (public domain, vendored here; validated against
 *   RFC 7748 §5.2 vectors in test_wg.c).
 * - AEAD: test-only ChaCha20-Poly1305 written from RFC 8439 (validated
 *   against its official vectors before use).
 * - Random: deterministic LCG so handshake tests are reproducible.
 *
 * NEVER linked into firmware — production uses wg_crypto_mbedtls.c.
 */

#include <string.h>

#include "chacha20poly1305_rfc8439.h"
#include "tweetnacl.h"
#include "src/wg/wg.h"

static uint64_t host_rng_state = 0x243F6A8885A308D3ull;

static void lcg_fill(uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* 64-bit LCG (Knuth MMIX constants); good enough to exercise
         * protocol paths deterministically, never used in firmware. */
        host_rng_state = host_rng_state * 6364136223846793005ull +
                         1442695040888963407ull;
        out[i] = (uint8_t)(host_rng_state >> 33);
    }
}

void tsnode_test_host_rng_seed(uint64_t seed)
{
    host_rng_state = seed;
}

static tsnode_err_t host_dh(uint8_t shared[32], const uint8_t priv[32],
                            const uint8_t pub[32])
{
    if (crypto_scalarmult_curve25519(shared, priv, pub) != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    return TSNODE_OK;
}

static tsnode_err_t host_pubkey(uint8_t pub[32], const uint8_t priv[32])
{
    if (crypto_scalarmult_curve25519_base(pub, priv) != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    return TSNODE_OK;
}

static tsnode_err_t host_seal(uint8_t *out_ct, const uint8_t key[32],
                              const uint8_t nonce[12], const uint8_t *aad,
                              size_t aad_len, const uint8_t *pt,
                              size_t pt_len)
{
    t_aead_seal(out_ct, key, nonce, aad, aad_len, pt, pt_len);
    return TSNODE_OK;
}

static tsnode_err_t host_open(uint8_t *out_pt, const uint8_t key[32],
                              const uint8_t nonce[12], const uint8_t *aad,
                              size_t aad_len, const uint8_t *ct,
                              size_t ct_len)
{
    return t_aead_open(out_pt, key, nonce, aad, aad_len, ct, ct_len) == 0
               ? TSNODE_OK
               : TSNODE_ERR_CRYPTO;
}

static tsnode_err_t host_random(uint8_t *out, size_t len)
{
    lcg_fill(out, len);
    return TSNODE_OK;
}

const tsnode_wg_crypto_t *tsnode_wg_crypto_host(void)
{
    static const tsnode_wg_crypto_t host_backend = {
        .dh = host_dh,
        .pubkey = host_pubkey,
        .aead_seal = host_seal,
        .aead_open = host_open,
        .random = host_random,
    };
    return &host_backend;
}

/* TweetNaCl calls randombytes() for box/sign operations that we never
 * actually use, but the symbol must be defined at link time. */
void randombytes(unsigned char *buf, unsigned long long len)
{
    lcg_fill(buf, (size_t)len);
}
