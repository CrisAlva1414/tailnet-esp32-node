/*
 * Core de tsnode: máquina de estados mínima.
 *
 * Este archivo es C puro: sin headers de ESP-IDF (regla dura de ADR-0006,
 * verificada por el guard de includes en CI). El acceso a plataforma es
 * exclusivamente vía src/port/tsnode_port.h.
 */

#include <stddef.h>

#include "tsnode.h"

#include "tsnode_err.h"

static tsnode_state_t s_state = TSNODE_STATE_STOPPED;

const char *tsnode_err_name(tsnode_err_t err)
{
    switch (err) {
    case TSNODE_OK:
        return "TSNODE_OK";
    case TSNODE_ERR_INVALID_ARG:
        return "TSNODE_ERR_INVALID_ARG";
    case TSNODE_ERR_NOT_INITIALIZED:
        return "TSNODE_ERR_NOT_INITIALIZED";
    case TSNODE_ERR_INVALID_STATE:
        return "TSNODE_ERR_INVALID_STATE";
    case TSNODE_ERR_NO_MEMORY:
        return "TSNODE_ERR_NO_MEMORY";
    case TSNODE_ERR_TIMEOUT:
        return "TSNODE_ERR_TIMEOUT";
    case TSNODE_ERR_CRYPTO:
        return "TSNODE_ERR_CRYPTO";
    case TSNODE_ERR_STORAGE:
        return "TSNODE_ERR_STORAGE";
    case TSNODE_ERR_NETWORK:
        return "TSNODE_ERR_NETWORK";
    case TSNODE_ERR_PROVISIONING:
        return "TSNODE_ERR_PROVISIONING";
    case TSNODE_ERR_NOT_IMPLEMENTED:
        return "TSNODE_ERR_NOT_IMPLEMENTED";
    default:
        return "TSNODE_ERR_UNKNOWN";
    }
}

const char *tsnode_state_name(tsnode_state_t state)
{
    switch (state) {
    case TSNODE_STATE_STOPPED:
        return "STOPPED";
    case TSNODE_STATE_INITIALIZED:
        return "INITIALIZED";
    case TSNODE_STATE_STARTING:
        return "STARTING";
    case TSNODE_STATE_ONLINE:
        return "ONLINE";
    case TSNODE_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

tsnode_err_t tsnode_init(void)
{
    if (s_state != TSNODE_STATE_STOPPED) {
        return TSNODE_ERR_INVALID_STATE;
    }
    s_state = TSNODE_STATE_INITIALIZED;
    return TSNODE_OK;
}

tsnode_err_t tsnode_start(const tsnode_app_config_t *config)
{
    (void)config;
    if (s_state == TSNODE_STATE_STOPPED) {
        return TSNODE_ERR_NOT_INITIALIZED;
    }
    /*
     * La implementación completa está en tsnode_simple.c (app layer).
     * Este stub existe para mantener la interfaz pública del componente.
     * El usuario debe copiar tsnode_simple.c a su proyecto y usar esa
     * implementación. Ver docs/QUICKSTART.md.
     */
    return TSNODE_ERR_NOT_IMPLEMENTED;
}

tsnode_err_t tsnode_stop(void)
{
    s_state = TSNODE_STATE_STOPPED;
    return TSNODE_OK;
}

tsnode_err_t tsnode_state_get(tsnode_state_t *out_state)
{
    if (out_state == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    *out_state = s_state;
    return TSNODE_OK;
}
