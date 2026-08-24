/*
 * HMAC-BLAKE2s and HKDF-BLAKE2s helpers (ADR-0008 D4).
 *
 * HMAC construction follows RFC 2104 with BLAKE2s-256 as the hash.
 * The HKDF shape mirrors the one used by both protocol layers this
 * component implements:
 *   - Noise/ts2021 MixKey/Split (tailscale controlbase),
 *   - WireGuard KDF1/KDF2/KDF3 (verified against
 *     WireGuard/wireguard-go device/noise-helpers.go @ master,
 *     fetched 2026-08-24).
 *
 * Extract: prk = HMAC(key, ikm)
 * Expand:  t1 = HMAC(prk, 0x01)
 *          t2 = HMAC(prk, t1 || 0x02)
 *          t3 = HMAC(prk, t2 || 0x03)
 *
 * Pure C11, no platform headers (ADR-0006).
 */

#ifndef TSNODE_HMAC_BLAKE2S_H
#define TSNODE_HMAC_BLAKE2S_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HMAC-BLAKE2s-256 over a single input part. out is 32 bytes. */
void tsnode_hmac_blake2s_1(uint8_t out[32], const uint8_t *key,
                           size_t keylen, const uint8_t *a, size_t a_len);

/* HMAC-BLAKE2s-256 over two concatenated input parts (avoids buffer
 * copies when the message is naturally split). out is 32 bytes. */
void tsnode_hmac_blake2s_2(uint8_t out[32], const uint8_t *key,
                           size_t keylen, const uint8_t *a, size_t a_len,
                           const uint8_t *b, size_t b_len);

/*
 * HKDF with 1 output block (WireGuard KDF1 / mixKey shape):
 * prk = HMAC(key, ikm); t0 = HMAC(prk, 0x01).
 * ikm may be NULL when ikm_len == 0.
 */
void tsnode_hkdf_blake2s_1(uint8_t t0[32], const uint8_t key[32],
                           const uint8_t *ikm, size_t ikm_len);

/*
 * HKDF with 2 output blocks (WireGuard KDF2 / Noise MixKey shape):
 * prk = HMAC(key, ikm); t0 = HMAC(prk, 0x01); t1 = HMAC(prk, t0 || 0x02).
 * Any output pointer may alias nothing; all outputs written on success.
 * ikm may be NULL when ikm_len == 0 (WireGuard Split uses empty IKM).
 */
void tsnode_hkdf_blake2s_2(uint8_t t0[32], uint8_t t1[32],
                           const uint8_t key[32], const uint8_t *ikm,
                           size_t ikm_len);

/* HKDF with 3 output blocks (WireGuard KDF3, preshared-key step). */
void tsnode_hkdf_blake2s_3(uint8_t t0[32], uint8_t t1[32], uint8_t t2[32],
                           const uint8_t key[32], const uint8_t *ikm,
                           size_t ikm_len);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_HMAC_BLAKE2S_H */
