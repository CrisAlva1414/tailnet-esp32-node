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

/* Headers de ESP-IDF/newlib disparan -Wpedantic (#include_next, macros
 * variádicas GNU) incluso en dialecto gnu. Se suprime pedantic SOLO
 * durante su inclusión; todo el código propio queda bajo -Wpedantic
 * completo. Convención obligatoria: ver docs/format/c-style.md. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <stdio.h>

#include "esp_log.h"
#pragma GCC diagnostic pop

static const char *TAG = "tailnet_init";

void app_main(void)
{
    ESP_LOGI(TAG, "%s", "tailnet-esp32-node: build de inicializacion, sin logica de protocolo aun");
    ESP_LOGI(TAG, "%s", "ver AGENTS.md y docs/adr/ antes de continuar el desarrollo");
}
