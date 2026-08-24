/*
 * Header for the host-test crypto backend (vendor/wg_crypto_host.c).
 * Test-only: never linked into firmware.
 */

#ifndef WG_TEST_CRYPTO_HOST_H
#define WG_TEST_CRYPTO_HOST_H

#include <stdint.h>

#include "src/wg/wg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* vtable backed by TweetNaCl X25519 + RFC 8439 test AEAD. */
const tsnode_wg_crypto_t *tsnode_wg_crypto_host(void);

/* Re-seeds the deterministic LCG so handshake tests are reproducible. */
void tsnode_test_host_rng_seed(uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* WG_TEST_CRYPTO_HOST_H */
