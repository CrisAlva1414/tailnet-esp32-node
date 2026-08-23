/*
 * Cliente Tailscale: ciclo de vida completo (ADR-0008).
 *
 * Orquesta: fetch control key → Noise handshake → register → map poll.
 * Ejecuta como tarea FreeRTOS dedicada.
 *
 * Este archivo es C puro: sin headers de plataforma (ADR-0006).
 */

#ifndef TSNODE_CLIENT_H
#define TSNODE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Client state (visible to app for status reporting) */
typedef enum {
    TSNODE_CLIENT_IDLE = 0,
    TSNODE_CLIENT_FETCHING_KEY,
    TSNODE_CLIENT_HANDSHAKING,
    TSNODE_CLIENT_REGISTERING,
    TSNODE_CLIENT_MAP_SYNC,
    TSNODE_CLIENT_ONLINE,
    TSNODE_CLIENT_ERROR,
} tsnode_client_state_t;

/* Client configuration (set before starting) */
typedef struct {
    const char *control_host;       /* "controlplane.tailscale.com" */
    uint16_t    control_port;       /* 80 for HTTP upgrade */
    const char *auth_key;           /* tskey-auth-... */
    const char *hostname;           /* node hostname */
    uint8_t     machine_key_priv[32]; /* our machine key (or zeroed to generate) */
} tsnode_client_config_t;

/*
 * Start the Tailscale client task.
 * Requires: WiFi connected, auth_key provisioned.
 * config is copied internally; the caller can free it after this returns.
 */
tsnode_err_t tsnode_client_start(const tsnode_client_config_t *config);

/*
 * Stop the client and disconnect from tailnet.
 */
tsnode_err_t tsnode_client_stop(void);

/*
 * Get current client state (for status display).
 */
tsnode_err_t tsnode_client_state_get(tsnode_client_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_CLIENT_H */
