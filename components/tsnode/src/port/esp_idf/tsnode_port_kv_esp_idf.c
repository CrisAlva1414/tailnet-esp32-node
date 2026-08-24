/*
 * Implementación ESP-IDF del KV persistente (NVS) para tsnode_port.h.
 *
 * Los blobs de identidad viven en la partición NVS. Cuando el proyecto
 * habilita NVS encryption + flash encryption (ADR-0003), el contenido queda
 * cifrado en reposo sin cambios en esta capa.
 */

#include <stdbool.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "tsnode_port.h"

bool tsnode_port_kv_get(const char *ns, const char *key, uint8_t *out,
                        size_t len)
{
    if (ns == NULL || key == NULL || out == NULL || len == 0) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t got = len;
    bool ok = nvs_get_blob(h, key, out, &got) == ESP_OK && got == len;
    nvs_close(h);
    return ok;
}

bool tsnode_port_kv_set(const char *ns, const char *key, const uint8_t *val,
                        size_t len)
{
    if (ns == NULL || key == NULL || val == NULL || len == 0) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_blob(h, key, val, len) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void tsnode_port_kv_del(const char *ns, const char *key)
{
    if (ns == NULL || key == NULL) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    /* NOT_FOUND es resultado esperado: borrar lo inexistente es idempotente */
    (void)nvs_erase_key(h, key);
    (void)nvs_commit(h);
    nvs_close(h);
}
