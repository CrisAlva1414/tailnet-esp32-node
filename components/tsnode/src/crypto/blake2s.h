/*
 * BLAKE2s hash — clean-room implementation from RFC 7693.
 *
 * This file is part of tsnode's crypto layer (ADR-0008 D4).
 * mbedTLS 3.6.3 (ESP-IDF v5.5) does not include BLAKE2s.
 *
 * Two variants are implemented:
 *   - Unkeyed hash (1..32 byte output): Noise IK transcript hashing
 *     and HKDF construction.
 *   - Keyed hash (BLAKE2s-MAC, 1..32 byte output): WireGuard MAC1/MAC2
 *     message authentication (ADR-0008 D1.4). digest_length is an internal
 *     parameter of the hash per RFC 7693 — a 16-byte keyed MAC is NOT the
 *     truncation of the 32-byte digest.
 *
 * Test vectors: RFC 7693 Appendix B and the official BLAKE2 KAT
 * (tests/unit/protocol_vectors/blake2s_vectors.h, provenance documented).
 * MUST be verified in tests/unit/ before any protocol code depends on this.
 */

#ifndef TSNODE_BLAKE2S_H
#define TSNODE_BLAKE2S_H

#include <stddef.h>
#include <stdint.h>

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TSNODE_BLAKE2S_BLOCKBYTES  64
#define TSNODE_BLAKE2S_OUTBYTES    32
#define TSNODE_BLAKE2S_KEYBYTES    32
#define TSNODE_BLAKE2S_SALTBYTES   16
#define TSNODE_BLAKE2S_PERSONALBYTES 16

typedef struct {
    uint32_t h[8];          /* chaining state */
    uint64_t t[2];          /* message byte counters */
    uint64_t f[2];          /* finalization flags */
    uint8_t  buf[TSNODE_BLAKE2S_BLOCKBYTES];
    size_t   buflen;
    size_t   outlen;        /* digest output length (1..32) */
} tsnode_blake2s_ctx;

/*
 * Streaming API: init -> update (0..N times) -> final.
 * out must have at least ctx->outlen bytes.
 */
void tsnode_blake2s_init(tsnode_blake2s_ctx *ctx, size_t outlen);
void tsnode_blake2s_update(tsnode_blake2s_ctx *ctx, const uint8_t *in,
                           size_t inlen);
void tsnode_blake2s_final(tsnode_blake2s_ctx *ctx, uint8_t *out,
                          size_t outlen);

/*
 * One-shot convenience: Hash(inlen, in) -> out (outlen bytes).
 * outlen must be in 1..32.
 */
void tsnode_blake2s(uint8_t *out, size_t outlen, const uint8_t *in,
                    size_t inlen);

/*
 * Keyed init (RFC 7693 keyed hashing, BLAKE2s-MAC). keylen must be in
 * 1..TSNODE_BLAKE2S_KEYBYTES; the zero-padded key block is absorbed as
 * the first data block, before any message input. outlen 1..32.
 */
tsnode_err_t tsnode_blake2s_init_key(tsnode_blake2s_ctx *ctx, size_t outlen,
                                     const uint8_t *key, size_t keylen);

/*
 * One-shot keyed convenience: MAC(key, in) -> out. Same parameter rules
 * as tsnode_blake2s_init_key.
 */
tsnode_err_t tsnode_blake2s_keyed(uint8_t *out, size_t outlen,
                                  const uint8_t *key, size_t keylen,
                                  const uint8_t *in, size_t inlen);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_BLAKE2S_H */
