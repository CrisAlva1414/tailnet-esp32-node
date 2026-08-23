/*
 * Cliente Tailscale: ciclo de vida completo (ADR-0008).
 *
 * Orquesta: fetch control key → Noise handshake → register → map poll.
 * Ejecuta como tarea FreeRTOS dedicada.
 */

#include "tsnode_client.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ts2021.h"
#include "tsnode_map.h"
#include "tsnode_port.h"
#include "tsnode_register.h"

#define TAG "tsnode_client"

#define CONTROL_KEY_BUF_SIZE 512
#define REGISTER_BUF_SIZE    1024
#define MAP_REQUEST_BUF_SIZE 1024
#define MAP_RESPONSE_BUF_SIZE 8192

static tsnode_client_state_t s_state = TSNODE_CLIENT_IDLE;
static tsnode_client_config_t s_config;
static ts2021_conn_t s_conn;
static uint8_t s_node_key[32];  /* WireGuard identity key */

/* ---- State management ---- */

static void set_state(tsnode_client_state_t state)
{
    s_state = state;
    TSNODE_LOGI(TAG, "state -> %d", (int)state);
}

/* ---- Fetch control server public key from /key?v=145 ---- */

static tsnode_err_t fetch_control_key(uint8_t key_out[32])
{
    set_state(TSNODE_CLIENT_FETCHING_KEY);

    tsnode_port_socket_t *sock = NULL;
    tsnode_err_t err = tsnode_port_tls_connect(&sock, s_config.control_host,
                                                443, 10000);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "TLS connect to %s failed: %d",
                    s_config.control_host, err);
        return err;
    }

    /* Send HTTPS GET /key?v=145 */
    char request[256];
    int n = snprintf(request, sizeof(request),
                     "GET /key?v=145 HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: tsnode/0.1.0\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     s_config.control_host);
    err = tsnode_port_socket_write(sock, (const uint8_t *)request,
                                    (size_t)n, 5000);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "send GET /key failed: %d", err);
        tsnode_port_socket_close(sock);
        return err;
    }

    /* Read response */
    uint8_t resp_buf[CONTROL_KEY_BUF_SIZE];
    size_t total = 0;
    size_t nread;

    while (total < sizeof(resp_buf) - 1) {
        err = tsnode_port_socket_read(sock, resp_buf + total,
                                       sizeof(resp_buf) - total - 1,
                                       &nread, 10000);
        if (err != TSNODE_OK && err != TSNODE_ERR_NETWORK) {
            break;
        }
        total += nread;
        if (nread == 0) break;
    }
    tsnode_port_socket_close(sock);

    resp_buf[total] = '\0';

    /* Parse HTTP response: find the JSON body after \r\n\r\n */
    const char *body = strstr((const char *)resp_buf, "\r\n\r\n");
    if (body == NULL) {
        TSNODE_LOGE(TAG, "no HTTP body in /key response");
        return TSNODE_ERR_NETWORK;
    }
    body += 4;

    /* Extract "PublicKey":"noise+keybase:<hex>" */
    const char *pk_marker = "\"PublicKey\":\"noise+keybase:";
    const char *pk = strstr(body, pk_marker);
    if (pk == NULL) {
        TSNODE_LOGE(TAG, "PublicKey not found in /key response");
        return TSNODE_ERR_NETWORK;
    }
    pk += strlen(pk_marker);

    /* Decode hex (64 chars = 32 bytes) */
    for (int i = 0; i < 32; i++) {
        int hi = -1, lo = -1;
        char c;

        c = pk[i * 2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;

        c = pk[i * 2 + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;

        if (hi < 0 || lo < 0) {
            TSNODE_LOGE(TAG, "invalid hex in PublicKey");
            return TSNODE_ERR_CRYPTO;
        }
        key_out[i] = (uint8_t)((hi << 4) | lo);
    }

    TSNODE_LOGI(TAG, "control key fetched OK");
    return TSNODE_OK;
}

/* ---- Full connection sequence ---- */

static tsnode_err_t do_connect(void)
{
    tsnode_err_t err;

    /* Step 1: Fetch control server public key */
    uint8_t control_key[32];
    err = fetch_control_key(control_key);
    if (err != TSNODE_OK) return err;

    /* Step 2: Generate machine key if not provided */
    bool need_gen = true;
    for (int i = 0; i < 32; i++) {
        if (s_config.machine_key_priv[i] != 0) {
            need_gen = false;
            break;
        }
    }
    if (need_gen) {
        err = tsnode_port_random_bytes(s_config.machine_key_priv, 32);
        if (err != TSNODE_OK) return err;
        TSNODE_LOGI(TAG, "generated new machine key");
    }

    /* Step 2b: Generate node key (WireGuard identity) from random */
    err = tsnode_port_random_bytes(s_node_key, 32);
    if (err != TSNODE_OK) return err;

    /* Step 3: TCP connect to control plane */
    set_state(TSNODE_CLIENT_HANDSHAKING);
    tsnode_port_socket_t *sock = NULL;
    err = tsnode_port_tcp_connect(&sock, s_config.control_host,
                                   s_config.control_port, 10000);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "TCP connect failed: %d", err);
        return err;
    }

    /* Step 4: HTTP upgrade to ts2021 */
    char upgrade_req[256];
    int n = snprintf(upgrade_req, sizeof(upgrade_req),
                     "GET /ts2021 HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: tailscale-control-protocol\r\n"
                     "Connection: Upgrade\r\n"
                     "\r\n",
                     s_config.control_host);
    err = tsnode_port_socket_write(sock, (const uint8_t *)upgrade_req,
                                    (size_t)n, 5000);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "send upgrade request failed");
        tsnode_port_socket_close(sock);
        return err;
    }

    /* Read HTTP 101 response */
    uint8_t resp[256];
    size_t resp_total = 0;
    size_t nread;
    bool got_101 = false;

    while (resp_total < sizeof(resp) - 1) {
        err = tsnode_port_socket_read(sock, resp + resp_total,
                                       sizeof(resp) - resp_total - 1,
                                       &nread, 10000);
        if (err != TSNODE_OK) break;
        resp_total += nread;
        resp[resp_total] = '\0';

        if (strstr((const char *)resp, "\r\n\r\n") != NULL) {
            got_101 = (strstr((const char *)resp, "101") != NULL);
            break;
        }
        if (nread == 0) break;
    }

    if (!got_101) {
        TSNODE_LOGE(TAG, "HTTP upgrade failed (no 101)");
        tsnode_port_socket_close(sock);
        return TSNODE_ERR_NETWORK;
    }
    TSNODE_LOGI(TAG, "HTTP 101 Switching Protocols OK");

    /* Step 5: Noise IK handshake */
    err = ts2021_handshake_client(&s_conn, s_config.machine_key_priv,
                                   control_key, 1, sock);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "Noise handshake failed: %d", err);
        return err;
    }

    /* Step 6: Register with auth key */
    set_state(TSNODE_CLIENT_REGISTERING);
    char reg_buf[REGISTER_BUF_SIZE];
    size_t reg_len;
    err = tsnode_register_build_request(reg_buf, sizeof(reg_buf), &reg_len,
                                         s_node_key,
                                         s_config.machine_key_priv,
                                         s_config.auth_key,
                                         s_config.hostname,
                                         145); /* CurrentCapabilityVersion */
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "build RegisterRequest failed: %d", err);
        return err;
    }

    /* Send RegisterRequest over record layer */
    err = ts2021_record_send(&s_conn, (const uint8_t *)reg_buf, reg_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "send RegisterRequest failed: %d", err);
        return err;
    }

    /* Receive RegisterResponse */
    uint8_t reg_resp[2048];
    size_t reg_resp_len;
    err = ts2021_record_recv(&s_conn, reg_resp, sizeof(reg_resp),
                              &reg_resp_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "recv RegisterResponse failed: %d", err);
        return err;
    }
    reg_resp[reg_resp_len] = '\0';

    tsnode_register_response_t reg_result;
    err = tsnode_register_parse_response(&reg_result,
                                          (const char *)reg_resp,
                                          reg_resp_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "parse RegisterResponse failed: %d", err);
        return err;
    }

    if (reg_result.error[0] != '\0') {
        TSNODE_LOGE(TAG, "registration error: %s", reg_result.error);
        return TSNODE_ERR_NETWORK;
    }

    if (reg_result.auth_url[0] != '\0') {
        TSNODE_LOGE(TAG, "interactive auth required: %s", reg_result.auth_url);
        return TSNODE_ERR_PROVISIONING;
    }

    if (!reg_result.machine_authorized) {
        TSNODE_LOGW(TAG, "machine not yet authorized, waiting...");
        /* In real Tailscale, this means the node needs approval.
         * For v1, we report this and continue polling. */
    }

    TSNODE_LOGI(TAG, "registration OK");

    /* Step 7: Map sync */
    set_state(TSNODE_CLIENT_MAP_SYNC);
    uint8_t zero_disco[32] = {0};
    char map_buf[MAP_REQUEST_BUF_SIZE];
    size_t map_len;
    err = tsnode_map_build_request(map_buf, sizeof(map_buf), &map_len,
                                    s_node_key,
                                    zero_disco,
                                    s_config.hostname,
                                    145, false);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "build MapRequest failed: %d", err);
        return err;
    }

    err = ts2021_record_send(&s_conn, (const uint8_t *)map_buf, map_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "send MapRequest failed: %d", err);
        return err;
    }

    uint8_t map_resp[MAP_RESPONSE_BUF_SIZE];
    size_t map_resp_len;
    err = ts2021_record_recv(&s_conn, map_resp, sizeof(map_resp),
                              &map_resp_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "recv MapResponse failed: %d", err);
        return err;
    }
    map_resp[map_resp_len] = '\0';

    tsnode_map_netmap_t netmap;
    err = tsnode_map_parse_response(&netmap, (const char *)map_resp,
                                     map_resp_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "parse MapResponse failed: %d", err);
        return err;
    }

    TSNODE_LOGI(TAG, "netmap: %u peers, self=%s",
                netmap.peer_count, netmap.self_ip);

    set_state(TSNODE_CLIENT_ONLINE);
    return TSNODE_OK;
}

/* ---- Task entry point (called by port task) ---- */

static void client_task(void *arg)
{
    (void)arg;
    TSNODE_LOGI(TAG, "client task started");

    tsnode_err_t err = do_connect();
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "connect failed: %s (%d)",
                    tsnode_err_name(err), err);
        set_state(TSNODE_CLIENT_ERROR);
    }

    /* TODO: map polling loop, reconnection logic */

    /* Task self-terminates by returning */
}

/* ---- Public API ---- */

tsnode_err_t tsnode_client_start(const tsnode_client_config_t *config)
{
    if (config == NULL || config->auth_key == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    if (s_state != TSNODE_CLIENT_IDLE && s_state != TSNODE_CLIENT_ERROR) {
        return TSNODE_ERR_INVALID_STATE;
    }

    memcpy(&s_config, config, sizeof(s_config));

    /* Default control plane */
    if (s_config.control_host == NULL) {
        s_config.control_host = "controlplane.tailscale.com";
    }
    if (s_config.control_port == 0) {
        s_config.control_port = 80;
    }

    set_state(TSNODE_CLIENT_FETCHING_KEY);

    /* Create task via port (4096 bytes stack, priority 5) */
    tsnode_err_t err = tsnode_port_task_create(client_task, NULL,
                                                "tsclient", 4096, 5);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "task create failed: %d", err);
        return err;
    }

    return TSNODE_OK;
}

tsnode_err_t tsnode_client_stop(void)
{
    ts2021_conn_close(&s_conn);
    set_state(TSNODE_CLIENT_IDLE);
    return TSNODE_OK;
}

tsnode_err_t tsnode_client_state_get(tsnode_client_state_t *out_state)
{
    if (out_state == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    *out_state = s_state;
    return TSNODE_OK;
}
