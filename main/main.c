/*
 * App de referencia del banco de pruebas.
 *
 * Capa de aplicación (ADR-0006): Wi-Fi + consola de provisioning viven
 * acá, no en el componente. El cliente Tailscale se arranca después de
 * WiFi y provisioning exitosos.
 */

#include <nvs_flash.h>

#include "esp_log.h"

#include "console.h"
#include "prov_store.h"
#include "tsnode.h"
#include "wifi_app.h"

static const char *TAG = "tailnet_init";

static void init_nvs_or_panic(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requiere formateo (%s)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init -> %s: sin NVS no hay provisioning",
                 esp_err_to_name(err));
        return;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s", "tailnet-esp32-node: app de referencia del banco");

    init_nvs_or_panic();

    tsnode_err_t terr = tsnode_init();
    ESP_LOGI(TAG, "tsnode_init -> %s", tsnode_err_name(terr));

    terr = wifi_app_start();
    ESP_LOGI(TAG, "wifi_app_start -> %s", tsnode_err_name(terr));

    console_start();
}
