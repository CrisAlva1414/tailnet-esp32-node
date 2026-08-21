/*
 * Interfaz interna del core de tsnode hacia la plataforma (ADR-0006).
 *
 * El core NUNCA incluye headers de ESP-IDF/FreeRTOS/lwIP: todo acceso a
 * plataforma pasa por esta interfaz. Implementaciones:
 *   - src/port/esp_idf/  — ESP-IDF real.
 *   - tests/unit/        — mocks host-side (ahí corre el core con
 *                          -Wpedantic completo, ver docs/format/c-style.md).
 *
 * La interfaz se diseña desde las necesidades del core, no calca APIs del
 * SDK (ADR-0006, consecuencias de seguridad). Ampliarla exige justificarlo
 * desde ese criterio.
 */

#ifndef TSNODE_PORT_H
#define TSNODE_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Entropía criptográficamente segura para generación de claves y nonces.
 * len debe ser > 0; out debe tener al menos len bytes. Falla
 * TSNODE_ERR_CRYPTO si la fuente de entropía no está lista — nunca retorna
 * pseudo-aleatoriedad débil "para no bloquear".
 */
tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len);

/*
 * Tiempo monotónico desde el boot, en milisegundos. Para timeouts y
 * medición de presupuesto de tiempo crypto (AGENTS.md §2.2). No depende del
 * reloj de pared ni de NTP.
 */
tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_PORT_H */
