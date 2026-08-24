/*
 * X25519 raw operations — using mbedTLS ECP with Curve25519.
 *
 * Provides raw 32-byte X25519 operations needed by Noise IK.
 * Uses mbedTLS ECP module which is always available (no Everest needed).
 */

#include "x25519_wrapper.h"

#include <string.h>

#include "esp_random.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform_util.h"

/* ESP-IDF entropy source for mbedTLS DRBG */
static int esp_entropy_func(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

/* Lazy-initialized DRBG for ECP operations that require f_rng */
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool s_rng_initialized;

static int ensure_rng(void)
{
    if (s_rng_initialized) return 0;
    mbedtls_ctr_drbg_init(&s_ctr_drbg);
    int ret = mbedtls_ctr_drbg_seed(&s_ctr_drbg, esp_entropy_func, NULL,
                                     (const unsigned char *)"tsnode", 6);
    if (ret != 0) return -1;
    s_rng_initialized = true;
    return 0;
}

int tsnode_x25519_keygen(uint8_t priv_out[32], uint8_t pub_out[32])
{
    if (priv_out == NULL || pub_out == NULL) {
        return -1;
    }

    if (ensure_rng() != 0) return -1;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    int ret;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    /* Setup Curve25519 group */
    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) goto cleanup;

    /* Generate keypair: d (private), Q (public) */
    ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                    mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) goto cleanup;

    /* Export private key as little-endian 32 bytes */
    ret = mbedtls_mpi_write_binary_le(&d, priv_out, 32);
    if (ret != 0) goto cleanup;

    /* Export public key as little-endian 32 bytes (x-coordinate only for Curve25519) */
    size_t olen;
    ret = mbedtls_ecp_point_write_binary(&grp, &Q,
                                          MBEDTLS_ECP_PF_COMPRESSED,
                                          &olen, pub_out, 32);
    if (ret != 0) goto cleanup;

cleanup:
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    return (ret == 0) ? 0 : -1;
}

int tsnode_x25519_publickey(const uint8_t priv[32], uint8_t pub_out[32])
{
    if (priv == NULL || pub_out == NULL) {
        return -1;
    }

    if (ensure_rng() != 0) return -1;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    int ret;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_read_binary_le(&d, priv, 32);
    if (ret != 0) goto cleanup;

    ret = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G,
                           mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) goto cleanup;

    size_t olen;
    ret = mbedtls_ecp_point_write_binary(&grp, &Q,
                                          MBEDTLS_ECP_PF_COMPRESSED,
                                          &olen, pub_out, 32);

cleanup:
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    return ret;
}

int tsnode_x25519_shared(uint8_t shared[32], const uint8_t priv[32],
                          const uint8_t pub[32])
{
    if (shared == NULL || priv == NULL || pub == NULL) {
        return -1;
    }

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_ecp_point R;
    mbedtls_mpi d;
    mbedtls_mpi r;
    int ret;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);

    /* Setup Curve25519 group */
    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) goto cleanup;

    /* Import private key from little-endian bytes */
    ret = mbedtls_mpi_read_binary_le(&d, priv, 32);
    if (ret != 0) goto cleanup;

    /* Import public key from compressed format */
    ret = mbedtls_ecp_point_read_binary(&grp, &Q, pub, 32);
    if (ret != 0) goto cleanup;

    /* Verify point is on curve */
    ret = mbedtls_ecp_check_pubkey(&grp, &Q);
    if (ret != 0) goto cleanup;

    /* Scalar multiplication: R = d * Q */
    if (ensure_rng() != 0) { ret = -1; goto cleanup; }
    ret = mbedtls_ecp_mul(&grp, &R, &d, &Q, mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) goto cleanup;

    /* Export x-coordinate as little-endian 32 bytes */
    size_t olen;
    ret = mbedtls_ecp_point_write_binary(&grp, &R,
                                          MBEDTLS_ECP_PF_COMPRESSED,
                                          &olen, shared, 32);
    if (ret != 0) goto cleanup;

    /* Check not point at infinity (all zeros) */
    uint8_t zero[32] = {0};
    if (memcmp(shared, zero, 32) == 0) {
        ret = -1;
    }

cleanup:
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_point_free(&R);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&r);
    return (ret == 0) ? 0 : -1;
}
