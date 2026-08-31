/*
 * Production crypto backend for the WireGuard core: mbedTLS ChaCha20-
 * Poly1305 + the existing X25519 wrapper, plus the ESP-IDF hardware RNG
 * through tsnode_port_random_bytes.
 *
 * Host unit tests do NOT link this file — they inject their own
 * doubles into the same vtable (ADR-0008 D3).
 */

#include "wg_crypto.h"

#include <string.h>

#include "mbedtls/chachapoly.h"
#include "mbedtls/platform_util.h"
#include "tsnode_port.h"
#include "x25519_wrapper.h"

static tsnode_err_t backend_dh(uint8_t shared[32], const uint8_t priv[32],
                               const uint8_t pub[32])
{
    if (tsnode_x25519_shared(shared, priv, pub) != 0) {
        return TSNODE_ERR_CRYPTO; /* includes all-zero result rejection */
    }
    return TSNODE_OK;
}

static tsnode_err_t backend_pubkey(uint8_t pub[32], const uint8_t priv[32])
{
    if (tsnode_x25519_publickey(priv, pub) != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    return TSNODE_OK;
}

static tsnode_err_t backend_seal(uint8_t *out_ct, const uint8_t key[32],
                                 const uint8_t nonce[12], const uint8_t *aad,
                                 size_t aad_len, const uint8_t *pt,
                                 size_t pt_len)
{
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int ret = mbedtls_chachapoly_setkey(&ctx, key);
    if (ret != 0) {
        mbedtls_chachapoly_free(&ctx);
        return TSNODE_ERR_CRYPTO;
    }
    uint8_t tag[16];
    ret = mbedtls_chachapoly_encrypt_and_tag(&ctx, pt_len, nonce, aad,
                                             aad_len, pt, out_ct, tag);
    mbedtls_chachapoly_free(&ctx);
    if (ret != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    memcpy(out_ct + pt_len, tag, sizeof(tag));
    return TSNODE_OK;
}

static tsnode_err_t backend_open(uint8_t *out_pt, const uint8_t key[32],
                                 const uint8_t nonce[12], const uint8_t *aad,
                                 size_t aad_len, const uint8_t *ct,
                                 size_t ct_len)
{
    if (ct_len < 16u) {
        return TSNODE_ERR_CRYPTO;
    }
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int ret = mbedtls_chachapoly_setkey(&ctx, key);
    if (ret != 0) {
        mbedtls_chachapoly_free(&ctx);
        return TSNODE_ERR_CRYPTO;
    }
    ret = mbedtls_chachapoly_auth_decrypt(&ctx, ct_len - 16u, nonce, aad,
                                          aad_len, ct + ct_len - 16u, ct,
                                          out_pt);
    mbedtls_chachapoly_free(&ctx);
    if (ret != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    return TSNODE_OK;
}

static tsnode_err_t backend_random(uint8_t *out, size_t len)
{
    return tsnode_port_random_bytes(out, len);
}

static tsnode_err_t backend_keygen(uint8_t priv[32], uint8_t pub[32])
{
    if (tsnode_x25519_keygen(priv, pub) != 0) {
        return TSNODE_ERR_CRYPTO;
    }
    return TSNODE_OK;
}

static void backend_zeroize(void *ptr, size_t len)
{
    if (ptr != NULL && len > 0) {
        mbedtls_platform_zeroize(ptr, len);
    }
}

static const tsnode_wg_crypto_t backend = {
    .dh = backend_dh,
    .pubkey = backend_pubkey,
    .aead_seal = backend_seal,
    .aead_open = backend_open,
    .random = backend_random,
    .keygen = backend_keygen,
    .zeroize = backend_zeroize,
};

const tsnode_wg_crypto_t *tsnode_wg_crypto_mbedtls(void)
{
    return &backend;
}
