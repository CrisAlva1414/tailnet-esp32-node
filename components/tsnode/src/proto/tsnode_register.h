/*
 * Registro de nodo ante el control plane (ADR-0008).
 *
 * Construye RegisterRequest JSON y parsea RegisterResponse.
 * Parser mínimo propio, sin heap allocation.
 */

#ifndef TSNODE_REGISTER_H
#define TSNODE_REGISTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed RegisterResponse (minimal fields needed) */
typedef struct {
    bool machine_authorized;
    bool node_key_expired;
    char auth_url[256];     /* non-empty = interactive auth required */
    char error[256];        /* non-empty = registration failed */
} tsnode_register_response_t;

/*
 * Build RegisterRequest JSON into buf.
 * node_key_pub es la clave PÚBLICA WireGuard del nodo (32 bytes crudos).
 * Tailscale CapabilityVersion = 145 (verified from source).
 * Returns TSNODE_ERR_NO_MEMORY if buf is too small.
 */
tsnode_err_t tsnode_register_build_request(char *buf, size_t buf_size,
                                           size_t *out_len,
                                           const uint8_t node_key_pub[32],
                                           const char *auth_key,
                                           const char *hostname,
                                           uint32_t capability_version);

/*
 * Parse RegisterResponse JSON into resp.
 * Minimal parser: extracts MachineAuthorized, AuthURL, Error,
 * NodeKeyExpired fields. Ignores unknown fields.
 */
tsnode_err_t tsnode_register_parse_response(tsnode_register_response_t *resp,
                                            const char *json, size_t json_len);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_REGISTER_H */
