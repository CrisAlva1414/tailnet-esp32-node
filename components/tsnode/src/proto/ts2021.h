/*
 * ts2021: cliente Noise IK + capa de registros para ts2021.
 *
 * Este archivo es C puro: sin headers de plataforma (ADR-0006).
 * Cipher suite: Noise_IK_25519_ChaChaPoly_BLAKE2s
 * Fuente primaria: tailscale/tailscale control/controlbase/
 */

#ifndef TSNODE_TS2021_H
#define TSNODE_TS2021_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tsnode_err.h"
#include "tsnode_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cipher suite name (verified from handshake.go) */
#define NOISE_PROTOCOL_NAME "Noise_IK_25519_ChaChaPoly_BLAKE2s"

/* Connection context after successful handshake */
typedef struct {
    uint8_t tx_key[32];              /* ChaCha20-Poly1305 tx key */
    uint8_t rx_key[32];              /* ChaCha20-Poly1305 rx key */
    uint64_t tx_counter;             /* Outgoing nonce counter */
    uint64_t rx_counter;             /* Incoming nonce counter */
    tsnode_port_socket_t *sock;      /* Underlying TCP socket */
    bool established;                /* true after Split() */
} ts2021_conn_t;

/*
 * Perform Noise IK handshake as client.
 * Sends initiation, receives response, derives session keys.
 *
 * Requires: machine_key_priv (32 bytes), control_key_pub (32 bytes),
 *           protocol_version (1 for ts2021), connected socket.
 *
 * On success, conn is populated with session keys and counters.
 */
tsnode_err_t ts2021_handshake_client(ts2021_conn_t *conn,
                                     const uint8_t machine_key_priv[32],
                                     const uint8_t control_key_pub[32],
                                     uint16_t protocol_version,
                                     tsnode_port_socket_t *sock);

/*
 * Send encrypted record (type=4 + length + ciphertext + tag).
 * Data is encrypted with tx_key and tx_counter.
 */
tsnode_err_t ts2021_record_send(ts2021_conn_t *conn, const uint8_t *data,
                                size_t len);

/*
 * Receive and decrypt one record.
 * Reads type/length header, decrypts with rx_key and rx_counter.
 * buf must be large enough for the plaintext (max 4093 bytes).
 */
tsnode_err_t ts2021_record_recv(ts2021_conn_t *conn, uint8_t *buf,
                                size_t buf_size, size_t *out_len);

/*
 * Close connection and clean up. Idempotent.
 */
void ts2021_conn_close(ts2021_conn_t *conn);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_TS2021_H */
