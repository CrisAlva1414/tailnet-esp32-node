/*
 * App Wi-Fi del banco de pruebas — implementación.
 */

#include "wifi_app.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <string.h>

#include "esp_log.h"

#include "prov_store.h"

#define WIFI_TAG "wifi_app"
#define RECONNECT_MIN_INTERVAL_MS 5000

static bool s_connected;
static bool s_config_applied;
static char s_ip[16];
static int64_t s_last_connect_attempt_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id,
                          void *data)
{
    /* arg sin uso: firma exigida por el registro de eventos. */
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /*
         * Solo conectar si ya hay config aplicada: al primer arranque del
         * stack la config aún no existe y un connect acá dispara una
         * asociación vacía que después bloquea el connect real
         * (ESP_ERR_WIFI_CONN, bug visto en banco).
         */
        if (!s_config_applied) {
            return;
        }
        s_last_connect_attempt_ms = now_ms();
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(WIFI_TAG, "esp_wifi_connect -> %s", esp_err_to_name(err));
        }
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
        /*
         * Reconexión con piso de tiempo: evita girar en seco contra un AP
         * ausente (el handler no debe bloquear). 5s entre intentos.
         */
        int64_t elapsed = now_ms() - s_last_connect_attempt_ms;
        if (elapsed >= RECONNECT_MIN_INTERVAL_MS) {
            s_last_connect_attempt_ms = now_ms();
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(WIFI_TAG, "reconnect -> %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(WIFI_TAG, "desconectado; reintento programado");
        }
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        ESP_LOGI(WIFI_TAG, "conectado, IP %s", s_ip);
        return;
    }
}

static tsnode_err_t init_stack_once(void)
{
    static bool stack_ready;
    if (stack_ready) {
        return TSNODE_OK;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "esp_netif_init -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(WIFI_TAG, "event_loop -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(WIFI_TAG, "create_default_wifi_sta fallo");
        return TSNODE_ERR_NETWORK;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "esp_wifi_init -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              event_handler, NULL, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  event_handler, NULL, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "register handlers -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "start -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    stack_ready = true;
    return TSNODE_OK;
}

tsnode_err_t wifi_app_start(void)
{
    char ssid[PROV_SSID_MAX_LEN];
    char psk[PROV_PSK_MAX_LEN];
    tsnode_err_t terr =
        prov_store_get_wifi(ssid, sizeof(ssid), psk, sizeof(psk));
    if (terr == TSNODE_ERR_NOT_INITIALIZED) {
        ESP_LOGI(WIFI_TAG, "sin credenciales; usar 'wifi set <ssid>' por consola");
        return TSNODE_OK;
    }
    if (terr != TSNODE_OK) {
        return terr;
    }
    terr = init_stack_once();
    if (terr != TSNODE_OK) {
        return terr;
    }
    return wifi_app_apply_credentials(ssid, psk);
}

tsnode_err_t wifi_app_apply_credentials(const char *ssid, const char *psk)
{
    if (ssid == NULL || psk == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    tsnode_err_t terr = init_stack_once();
    if (terr != TSNODE_OK) {
        return terr;
    }
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    /* Truncamiento imposible por validación previa de prov_store; se
     * acota igualmente por defensa en profundidad. */
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, psk, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "set_config -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    s_config_applied = true;
    s_last_connect_attempt_ms = now_ms();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "connect -> %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }
    ESP_LOGI(WIFI_TAG, "conectando a ssid '%s'", ssid);
    return TSNODE_OK;
}

bool wifi_app_is_connected(void)
{
    return s_connected;
}

void wifi_app_get_ip(char *ip_out, size_t ip_out_len)
{
    if (ip_out == NULL || ip_out_len == 0) {
        return;
    }
    strncpy(ip_out, s_ip, ip_out_len - 1);
    ip_out[ip_out_len - 1] = '\0';
}
