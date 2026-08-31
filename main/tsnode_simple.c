/*
 * tsnode_simple.c — Wrapper "just works" para tsnode.
 *
 * Orquesta: WiFi + credential storage + Tailscale client.
 * Copiar este archivo a tu proyecto para usar la API simple.
 *
 * Uso:
 *   tsnode_init();
 *   tsnode_start(&(tsnode_app_config_t){
 *       .wifi_ssid = "MiSSID",
 *       .wifi_psk = "MiPSK",
 *       .ts_auth_key = "tskey-auth-...",
 *   });
 */

#include "tsnode.h"
#include "tsnode_client.h"
#include "prov_store.h"
#include "wifi_app.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tsnode_simple";

/* Derive hostname from MAC: esp32-XXYYZZ */
static void derive_hostname(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "esp32-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
}

tsnode_err_t tsnode_start(const tsnode_app_config_t *config)
{
    if (config == NULL || config->wifi_ssid == NULL ||
        config->wifi_psk == NULL || config->ts_auth_key == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    /* Validate auth key prefix */
    if (strncmp(config->ts_auth_key, "tskey-auth-", 11) != 0) {
        ESP_LOGE(TAG, "auth key must start with 'tskey-auth-'");
        return TSNODE_ERR_INVALID_ARG;
    }

    /* 1. Store WiFi credentials in NVS */
    tsnode_err_t err = prov_store_save_wifi(config->wifi_ssid, config->wifi_psk);
    if (err != TSNODE_OK) {
        ESP_LOGE(TAG, "save wifi credentials failed: %d", err);
        return err;
    }

    /* 2. Store Tailscale auth key in NVS */
    err = prov_store_save_tskey(config->ts_auth_key);
    if (err != TSNODE_OK) {
        ESP_LOGE(TAG, "save auth key failed: %d", err);
        return err;
    }

    /* 3. Apply WiFi credentials and connect */
    err = wifi_app_apply_credentials(config->wifi_ssid, config->wifi_psk);
    if (err != TSNODE_OK) {
        ESP_LOGE(TAG, "wifi apply credentials failed: %d", err);
        return err;
    }

    /* 4. Wait for WiFi connection (max 15 seconds) */
    ESP_LOGI(TAG, "waiting for WiFi...");
    for (int i = 0; i < 150; i++) {
        if (wifi_app_is_connected()) {
            ESP_LOGI(TAG, "WiFi connected");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!wifi_app_is_connected()) {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return TSNODE_ERR_NETWORK;
    }

    /* 5. Build client config */
    tsnode_client_config_t client_cfg = {
        .control_host = config->control_host,
        .control_port = config->control_port,
        .auth_key = config->ts_auth_key,
    };
    memset(client_cfg.machine_key_priv, 0, sizeof(client_cfg.machine_key_priv));

    /* Hostname */
    char hostname_buf[TSNODE_CLIENT_HOSTNAME_MAX];
    if (config->hostname != NULL) {
        strncpy(hostname_buf, config->hostname, sizeof(hostname_buf) - 1);
        hostname_buf[sizeof(hostname_buf) - 1] = '\0';
    } else {
        derive_hostname(hostname_buf, sizeof(hostname_buf));
    }
    client_cfg.hostname = hostname_buf;

    /* WireGuard endpoint (WiFi IP + port 51820) */
    char ip_buf[16] = {0};
    wifi_app_get_ip(ip_buf, sizeof(ip_buf));
    if (ip_buf[0] != '\0') {
        unsigned a, b, c, d;
        if (sscanf(ip_buf, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            client_cfg.endpoint_ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                                     ((uint32_t)c << 8) | (uint32_t)d;
            client_cfg.endpoint_port = 51820;
            ESP_LOGI(TAG, "WG endpoint: %s:51820", ip_buf);
        }
    }

    /* Defaults */
    if (client_cfg.control_host == NULL) {
        client_cfg.control_host = "controlplane.tailscale.com";
    }
    if (client_cfg.control_port == 0) {
        client_cfg.control_port = 80;
    }

    /* 6. Start Tailscale client */
    err = tsnode_client_start(&client_cfg);
    if (err != TSNODE_OK) {
        ESP_LOGE(TAG, "client start failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Tailscale client started (hostname: %s)", client_cfg.hostname);
    return TSNODE_OK;
}
