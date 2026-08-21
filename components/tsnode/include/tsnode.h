/*
 * API pública de tsnode: cliente Tailscale mínimo para ESP32.
 *
 * Frontera del componente (ADR-0005): los proyectos consumidores solo
 * incluyen headers de este directorio. La implementación vive en src/ y
 * puede cambiar sin aviso entre versiones 0.x.
 *
 * Capas (ADR-0006): este header no expone nada de plataforma. El ciclo de
 * vida es intencionalmente mínimo hasta que el ADR de arquitectura de
 * protocolo defina las transiciones reales; start() retorna
 * TSNODE_ERR_NOT_IMPLEMENTED mientras esos ADRs (0002/0003) no estén
 * aceptados.
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
 * Estados visibles del nodo. Es el esqueleto mínimo para que una app pueda
 * consultar estado desde el día uno; las transiciones y estados intermedios
 * de provisioning/conexión se definen con el ADR de arquitectura de
 * protocolo.
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
 *
 * Contrato de concurrencia: llamar desde una única tarea (app_main o
 * equivalente). El contrato multithread se fija con el ADR de arquitectura;
 * hasta entonces no hay locking interno y así está documentado a propósito.
 */
tsnode_err_t tsnode_init(void);

/*
 * Arranca el cliente (registro/identidad/WireGuard). Requiere
 * TSNODE_STATE_INITIALIZED. Retorna TSNODE_ERR_NOT_IMPLEMENTED hasta que
 * ADR-0002 y ADR-0003 estén aceptados y exista el ADR de arquitectura de
 * protocolo.
 */
tsnode_err_t tsnode_start(void);

/*
 * Detiene el cliente y libera recursos de red. Válido desde cualquier
 * estado distinto de TSNODE_STATE_STOPPED. Idempotente.
 */
tsnode_err_t tsnode_stop(void);

/*
 * Estado actual. out_state no puede ser NULL (TSNODE_ERR_INVALID_ARG).
 */
tsnode_err_t tsnode_state_get(tsnode_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_H */
