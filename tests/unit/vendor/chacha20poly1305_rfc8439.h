/*
 * Test-only ChaCha20-Poly1305 (RFC 8439) for host unit tests.
 *
 * Written from the RFC text with its official vectors as the acceptance
 * gate (tests/unit/protocol_vectors/wg_vectors.h). NEVER linked into
 * firmware: production uses mbedTLS via wg_crypto_mbedtls.c. Kept
 * deliberately small and readable over fast.
 */

#ifndef WG_TEST_CHACHA20POLY1305_RFC8439_H
#define WG_TEST_CHACHA20POLY1305_RFC8439_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raw ChaCha20 block function (RFC 8439 §2.3): 64 bytes of output. */
void t_chacha20_block(uint8_t out[64], const uint8_t key[32],
                      const uint8_t nonce[12], uint32_t counter);

/* AEAD seal per RFC 8439 §2.8: out = ct || tag (tag appended). */
void t_aead_seal(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
                 const uint8_t *aad, size_t aad_len, const uint8_t *pt,
                 size_t pt_len);

/* AEAD open: returns 0 on tag success, -1 otherwise; out gets pt only
 * on success (buffer zeroed on failure). */
int t_aead_open(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
                const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                size_t ct_len);

#ifdef __cplusplus
}
#endif

#endif /* WG_TEST_CHACHA20POLY1305_RFC8439_H */
