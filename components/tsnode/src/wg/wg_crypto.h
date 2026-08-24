/*
 * Private header for the mbedTLS WireGuard crypto backend. The vtable
 * type itself is public (wg.h); only backend construction is private.
 */

#ifndef TSNODE_WG_CRYPTO_H
#define TSNODE_WG_CRYPTO_H

#include "wg.h"

#ifdef __cplusplus
extern "C" {
#endif

const tsnode_wg_crypto_t *tsnode_wg_crypto_mbedtls(void);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_WG_CRYPTO_H */
