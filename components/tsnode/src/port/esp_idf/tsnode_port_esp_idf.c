/*
 * Implementación ESP-IDF del port de tsnode (ADR-0006).
 *
 * Único lugar del componente donde se permiten headers de plataforma.
 */

#include <esp_err.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "tsnode_port.h"

tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len)
{
    if (out == NULL || len == 0) {
        return TSNODE_ERR_INVALID_ARG;
    }
    /*
     * esp_fill_random: entropía del hardware RNG. Con RF activo (Wi-Fi on)
     * la fuente es de calidad criptográfica; antes de activar RF el
     * bootloader aporta entropía de ruido térmico — suficiente para
     * nonces de sesión, no para claves de largo plazo. La generación de
     * node key ocurre con Wi-Fi ya arriba; verificado en el ADR de
     * arquitectura de protocolo cuando exista.
     */
    esp_fill_random(out, len);
    return TSNODE_OK;
}

tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms)
{
    if (out_ms == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    /* esp_timer es monotónico, no depende de NTP ni ajustes de reloj. */
    *out_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    return TSNODE_OK;
}
