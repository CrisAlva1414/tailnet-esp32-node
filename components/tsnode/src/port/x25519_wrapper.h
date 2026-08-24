/*
 * X25519 raw operations — wrapper over mbedTLS Everest X25519.
 *
 * mbedTLS's public X25519 API uses TLS framing (length-prefixed).
 * For Noise, we need raw 32-byte operations. This wrapper provides
 * a clean interface and handles the format conversion internally.
 *
 * This file IS platform-specific (uses mbedTLS) — it lives in src/port/.
 */

#ifndef TSNODE_X25519Wrapper_H
#define TSNODE_X25519Wrapper_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * X25519 key agreement: shared = X25519(priv, pub).
 * All buffers must be 32 bytes.
 * Returns 0 on success, -1 on error (invalid key, all-zero input).
 */
int tsnode_x25519_shared(uint8_t shared[32], const uint8_t priv[32],
                          const uint8_t pub[32]);

/*
 * Generate X25519 keypair: priv (random), pub = X25519(priv, G).
 * priv_out and pub_out must be 32 bytes each.
 * Returns 0 on success.
 */
int tsnode_x25519_keygen(uint8_t priv_out[32], uint8_t pub_out[32]);

/*
 * Derive public key from existing private key: pub = X25519(priv, G).
 * priv must be 32 bytes, pub_out must be 32 bytes.
 * Returns 0 on success.
 */
int tsnode_x25519_publickey(const uint8_t priv[32], uint8_t pub_out[32]);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_X25519Wrapper_H */
