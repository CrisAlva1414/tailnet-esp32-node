/*
 * Punto de entrada mínimo de inicialización.
 *
 * Deliberadamente NO contiene todavía lógica de Wi-Fi, Tailscale, ni
 * criptografía. Ver docs/sessions/2026-08-20-init-repo.md: el código de
 * protocolo real empieza después de que ADR-0002 (modelo de amenaza) y
 * ADR-0003 (estrategia de almacenamiento de claves) pasen de "propuesto"
 * a "aceptado". No se debe agregar funcionalidad aquí sin ese ADR previo.
 *
 * Ver AGENTS.md antes de modificar este archivo.
 */

#include <stdio.h>

#include "esp_log.h"

#include "tsnode.h"

static const char *TAG = "tailnet_init";

void app_main(void)
{
    ESP_LOGI(TAG, "%s", "tailnet-esp32-node: build de inicializacion");

    tsnode_err_t err = tsnode_init();
    ESP_LOGI(TAG, "tsnode_init -> %s", tsnode_err_name(err));

    tsnode_state_t state = TSNODE_STATE_STOPPED;
    err = tsnode_state_get(&state);
    if (err == TSNODE_OK) {
        ESP_LOGI(TAG, "tsnode state -> %s", tsnode_state_name(state));
    } else {
        ESP_LOGE(TAG, "tsnode_state_get -> %s", tsnode_err_name(err));
    }

    err = tsnode_start();
    /* Bloqueado por diseño hasta ADR-0002/0003 aceptados: ver tsnode_start()
     * y docs/sessions/. No es un error de runtime, es el estado del proyecto. */
    ESP_LOGI(TAG, "tsnode_start -> %s", tsnode_err_name(err));
}
