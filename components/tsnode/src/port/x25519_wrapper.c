/*
 * X25519 raw operations — direct use of Hacl_Curve25519 from mbedTLS/Everest.
 *
 * Provides raw 32-byte X25519 operations needed by Noise IK.
 * This IS platform-specific (mbedTLS + Everest) — lives in src/port/.
 */

#include "x25519_wrapper.h"

#include <string.h>

#include "mbedtls/platform.h"
#include "mbedtls/ctr_drbg.h"
#include "everest/Hacl_Curve25519.h"

int tsnode_x25519_keygen(uint8_t priv_out[32], uint8_t pub_out[32])
{
    if (priv_out == NULL || pub_out == NULL) {
        return -1;
    }

    /* Generate 32 random bytes for private key */
    int ret = mbedtls_ctr_drbg_random(NULL, priv_out, 32);
    if (ret != 0) {
        return -1;
    }

    /* Clamp private key per X25519 spec (RFC 7748 Section 5) */
    priv_out[0]  &= 248;
    priv_out[31] &= 127;
    priv_out[31] |= 64;

    /* Compute public key: pub = X25519(priv, G) */
    uint8_t base[32] = {0};
    base[0] = 9;
    uint8_t priv_tmp[32];
    memcpy(priv_tmp, priv_out, 32);
    Hacl_Curve25519_crypto_scalarmult(pub_out, priv_tmp, base);
    mbedtls_platform_zeroize(priv_tmp, sizeof(priv_tmp));

    /* Verify not point at infinity */
    uint8_t zero[32] = {0};
    if (memcmp(pub_out, zero, 32) == 0) {
        return -1;
    }

    return 0;
}

int tsnode_x25519_shared(uint8_t shared[32], const uint8_t priv[32],
                          const uint8_t pub[32])
{
    if (shared == NULL || priv == NULL || pub == NULL) {
        return -1;
    }

    /* Reject all-zero public key (invalid point) */
    uint8_t zero[32] = {0};
    if (memcmp(pub, zero, 32) == 0) {
        return -1;
    }

    uint8_t pub_tmp[32];
    uint8_t priv_tmp[32];
    memcpy(pub_tmp, pub, 32);
    memcpy(priv_tmp, priv, 32);
    Hacl_Curve25519_crypto_scalarmult(shared, priv_tmp, pub_tmp);
    mbedtls_platform_zeroize(pub_tmp, sizeof(pub_tmp));
    mbedtls_platform_zeroize(priv_tmp, sizeof(priv_tmp));

    /* Reject shared secret = 0 (invalid) */
    if (memcmp(shared, zero, 32) == 0) {
        return -1;
    }

    return 0;
}
