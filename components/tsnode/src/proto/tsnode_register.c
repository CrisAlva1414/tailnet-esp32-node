/*
 * Registro de nodo ante el control plane (ADR-0008).
 *
 * POST /machine/register sobre la conexión Noise ya establecida.
 * RegisterRequest es JSON; RegisterResponse es JSON.
 * Parser mínimo propio (sin dependencias de librería JSON).
 *
 * Este archivo es C puro: sin headers de plataforma (ADR-0006).
 */

#include "tsnode_register.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ts2021.h"
#include "tsnode_port.h"

#define TAG "tsnode_reg"

/* ---- Hex encoding for key bytes ---- */

static void bytes_to_hex(char *out, size_t out_cap, const uint8_t *in,
                         size_t inlen)
{
    if (out_cap < inlen * 2 + 1) return;
    for (size_t i = 0; i < inlen; i++) {
        snprintf(out + i * 2, 3, "%02x", in[i]);
    }
}

/* ---- Public API ---- */

tsnode_err_t tsnode_register_build_request(char *buf, size_t buf_size,
                                           size_t *out_len,
                                           const uint8_t node_key[32],
                                           const uint8_t machine_key[32],
                                           const char *auth_key,
                                           const char *hostname,
                                           uint32_t capability_version)
{
    if (buf == NULL || buf_size == 0 || out_len == NULL ||
        node_key == NULL || machine_key == NULL || auth_key == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    char node_hex[65], mach_hex[65];
    bytes_to_hex(node_hex, sizeof(node_hex), node_key, 32);
    bytes_to_hex(mach_hex, sizeof(mach_hex), machine_key, 32);

    /* Build JSON manually — no dynamic allocation.
     * Format matches tailcfg.RegisterRequest (verified from source). */
    size_t pos = 0;
    int n;

    buf[pos++] = '{';

    n = snprintf(buf + pos, buf_size - pos,
                 "\"Version\":%" PRIu32,
                 capability_version);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    buf[pos++] = ',';
    n = snprintf(buf + pos, buf_size - pos,
                 "\"NodeKey\":\"nodekey:%s\"", node_hex);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    buf[pos++] = ',';
    n = snprintf(buf + pos, buf_size - pos,
                 "\"Auth\":{\"AuthKey\":\"%s\"}", auth_key);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    /* Hostinfo anidado (tailcfg.Hostinfo): RegisterRequest no tiene un campo
     * Hostname de nivel superior — el hostname va en Hostinfo.Hostname y el
     * OS en Hostinfo.OS ("linux", igual que tailscaled; validado contra
     * producción con el probe h2, sesión 2026-08-23). */
    if (hostname != NULL && hostname[0] != '\0') {
        buf[pos++] = ',';
        n = snprintf(buf + pos, buf_size - pos,
                     "\"Hostinfo\":{\"OS\":\"linux\",\"Hostname\":\"%s\"}",
                     hostname);
        if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
        pos += n;
    }

    buf[pos++] = '}';
    buf[pos] = '\0';

    *out_len = pos;
    return TSNODE_OK;
}

tsnode_err_t tsnode_register_parse_response(tsnode_register_response_t *resp,
                                            const char *json, size_t json_len)
{
    if (resp == NULL || json == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    memset(resp, 0, sizeof(*resp));

    /* Minimal parser: look for key patterns without a JSON library.
     * This is intentionally simple — we control what the server sends
     * and only need a few fields. For security, we never trust string
     * lengths from the wire without bounds checking. */

    /* Check for "Error" field */
    const char *err_field = strstr(json, "\"Error\"");
    if (err_field != NULL) {
        /* Look for the string value after the colon */
        const char *val = strchr(err_field + 7, ':');
        if (val != NULL && *(val + 1) == '"') {
            val++;
            const char *end = strchr(val + 1, '"');
            if (end != NULL) {
                size_t len = (size_t)(end - val - 1);
                if (len >= sizeof(resp->error)) {
                    len = sizeof(resp->error) - 1;
                }
                memcpy(resp->error, val + 1, len);
                resp->error[len] = '\0';
                /* If there's a non-empty error, the registration failed */
                if (resp->error[0] != '\0') {
                    return TSNODE_OK;
                }
            }
        }
    }

    /* Check for "MachineAuthorized" */
    const char *auth_field = strstr(json, "\"MachineAuthorized\"");
    if (auth_field != NULL) {
        const char *val = strchr(auth_field + 19, ':');
        if (val != NULL) {
            resp->machine_authorized =
                (strncmp(val + 1, "true", 4) == 0);
        }
    }

    /* Check for "AuthURL" */
    const char *url_field = strstr(json, "\"AuthURL\"");
    if (url_field != NULL) {
        const char *val = strchr(url_field + 9, ':');
        if (val != NULL && *(val + 1) == '"') {
            val++;
            const char *end = strchr(val + 1, '"');
            if (end != NULL) {
                size_t len = (size_t)(end - val - 1);
                if (len >= sizeof(resp->auth_url)) {
                    len = sizeof(resp->auth_url) - 1;
                }
                memcpy(resp->auth_url, val + 1, len);
                resp->auth_url[len] = '\0';
            }
        }
    }

    /* Check for "NodeKeyExpired" */
    const char *exp_field = strstr(json, "\"NodeKeyExpired\"");
    if (exp_field != NULL) {
        const char *val = strchr(exp_field + 16, ':');
        if (val != NULL) {
            resp->node_key_expired =
                (strncmp(val + 1, "true", 4) == 0);
        }
    }

    return TSNODE_OK;
}
