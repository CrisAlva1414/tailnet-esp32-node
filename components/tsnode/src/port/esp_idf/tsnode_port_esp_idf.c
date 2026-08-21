/*
 * Implementación ESP-IDF del port de tsnode (ADR-0006).
 *
 * Único lugar del componente donde se permiten headers de plataforma.
 * Stubs por ahora: las implementaciones reales entran junto con los ADRs
 * de crypto/storage que las exigen, no antes.
 */

#include "tsnode_port.h"

tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len)
{
    /* Descarte explícito y seguro: stub previo al ADR de crypto; nunca
     * retornar entropía débil disfrazada de OK. */
    (void)out;
    (void)len;
    return TSNODE_ERR_NOT_IMPLEMENTED;
}

tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms)
{
    /* Ídem: stub hasta que el port real use esp_timer. */
    (void)out_ms;
    return TSNODE_ERR_NOT_IMPLEMENTED;
}
