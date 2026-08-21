/*
 * App Wi-Fi del banco de pruebas (capa aplicación, ADR-0006).
 *
 * STA con reconexión automática y rate-limit de intentos. Las
 * credenciales vienen de prov_store (NVS), nunca hardcodeadas (ADR-0007).
 */

#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Arranca el subsistema Wi-Fi si hay credenciales provisionadas; si no,
 * queda listo para que la consola las cargue y llama de nuevo.
 * Idempotente.
 */
tsnode_err_t wifi_app_start(void);

/* Aplica credenciales recién guardadas: configura y conecta sin reboot. */
tsnode_err_t wifi_app_apply_credentials(const char *ssid, const char *psk);

/* Estado para la consola. ip_out puede ser "" si no hay IP. */
bool wifi_app_is_connected(void);
void wifi_app_get_ip(char *ip_out, size_t ip_out_len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_APP_H */
