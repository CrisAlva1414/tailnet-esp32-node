/*
 * API pública de tsnode: cliente Tailscale mínimo para ESP32.
 *
 * Frontera del componente (ADR-0005): los proyectos consumidores solo
 * incluyen headers de este directorio. La implementación vive en src/ y
 * puede cambiar sin aviso entre versiones 0.x.
 *
 * Uso mínimo (ver docs/QUICKSTART.md):
 *
 *   tsnode_app_config_t cfg = {
 *       .wifi_ssid   = "MiSSID",
 *       .wifi_psk    = "MiPSK",
 *       .ts_auth_key = "tskey-auth-...",
 *   };
 *   tsnode_init();
 *   tsnode_start(&cfg);
 */

#ifndef TSNODE_H
#define TSNODE_H

#include <stdint.h>

#include "tsnode_config.h"
#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configuración de la aplicación. Solo tres campos son obligatorios.
 * hostname, control_host y control_port tienen defaults razonables.
 */
typedef struct {
    /* --- Requeridos --- */
    const char *wifi_ssid;       /* SSID de la red WiFi (1-32 chars) */
    const char *wifi_psk;        /* Contraseña WPA2 (8-63 chars) */
    const char *ts_auth_key;     /* Auth key de Tailscale ("tskey-auth-...") */

    /* --- Opcionales (NULL = default) --- */
    const char *hostname;        /* NULL → "esp32-XXYYZZ" derivado de MAC */
    const char *control_host;    /* NULL → "controlplane.tailscale.com" */
    uint16_t    control_port;    /* 0 → 80 */
} tsnode_app_config_t;

/*
 * Estados visibles del nodo.
 */
typedef enum {
    TSNODE_STATE_STOPPED = 0,
    TSNODE_STATE_INITIALIZED,
    TSNODE_STATE_STARTING,
    TSNODE_STATE_ONLINE,
    TSNODE_STATE_ERROR,
} tsnode_state_t;

/* Nombre legible del estado, para logs. Nunca retorna NULL. */
const char *tsnode_state_name(tsnode_state_t state);

/*
 * Inicializa el componente: valida configuración compile-time y deja el
 * nodo en TSNODE_STATE_INITIALIZED. No toca red ni crypto todavía.
 */
tsnode_err_t tsnode_init(void);

/*
 * Arranca WiFi + Tailscale client con la configuración dada.
 * Requiere: TSNODE_STATE_INITIALIZED, nvs_flash_init() ya llamado.
 *
 * Internamente:
 *   1. Guarda WiFi credentials y auth key en NVS
 *   2. Conecta WiFi (bloquea hasta conectar)
 *   3. Deriva hostname de MAC si no se provee
 *   4. Lanza el cliente Tailscale en background
 *
 * Retorna TSNODE_OK si todo arrancó correctamente.
 */
tsnode_err_t tsnode_start(const tsnode_app_config_t *config);

/*
 * Detiene el cliente y libera recursos de red.
 */
tsnode_err_t tsnode_stop(void);

/*
 * Estado actual. out_state no puede ser NULL.
 */
tsnode_err_t tsnode_state_get(tsnode_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_H */
