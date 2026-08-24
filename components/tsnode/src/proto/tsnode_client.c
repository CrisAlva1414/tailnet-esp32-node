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
#include "x25519_wrapper.h"
#include "base64.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "tsnode_client"

#define CONTROL_KEY_BUF_SIZE 1024
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

    /* Debug: log raw response for protocol development */
    TSNODE_LOGI(TAG, "raw /key response (%u bytes): %.200s",
                (unsigned)total, (const char *)resp_buf);

    /* Parse HTTP response: find the JSON body after \r\n\r\n */
    const char *body = strstr((const char *)resp_buf, "\r\n\r\n");
    if (body == NULL) {
        TSNODE_LOGE(TAG, "no HTTP body in /key response");
        return TSNODE_ERR_NETWORK;
    }
    body += 4;

    TSNODE_LOGI(TAG, "key response body (%u bytes): %s",
                (unsigned)strlen(body), body);

    /* Extract "publicKey":"mkey:<hex>" — Noise public key from control server.
     * JSON may have tabs/newlines, so search for the key without strict spacing. */
    const char *pk_marker = "\"publicKey\"";
    const char *pk = strstr(body, pk_marker);
    if (pk != NULL) {
        /* Skip past the key name and any whitespace/colon/quote */
        pk += strlen(pk_marker);
        while (*pk == ' ' || *pk == ':' || *pk == '\t' || *pk == '\n' || *pk == '"') pk++;
        /* Skip "mkey:" prefix if present */
        if (strncmp(pk, "mkey:", 5) == 0) pk += 5;

        TSNODE_LOGI(TAG, "publicKey hex start: %.70s", pk);
        /* Debug: dump all 64 hex chars as codes */
        for (int d = 0; d < 64; d++) {
            char ch = pk[d];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
                TSNODE_LOGE(TAG, "BAD hex[%d] = 0x%02x '%c'", d, (uint8_t)ch,
                            (ch >= 0x20 && ch < 0x7f) ? ch : '?');
            }
        }
    }
    if (pk == NULL) {
        TSNODE_LOGE(TAG, "PublicKey not found in /key response");
        return TSNODE_ERR_NETWORK;
    }

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
        /* Use mbedTLS keygen which applies proper X25519 clamping internally */
        uint8_t temp_pub[32];
        if (tsnode_x25519_keygen(s_config.machine_key_priv, temp_pub) != 0) {
            return TSNODE_ERR_CRYPTO;
        }
        TSNODE_LOGI(TAG, "generated new machine key");
    }

    /* Step 2b: Generate node key (WireGuard identity) from random */
    uint8_t node_key_pub_tmp[32];
    if (tsnode_x25519_keygen(s_node_key, node_key_pub_tmp) != 0) {
        return TSNODE_ERR_CRYPTO;
    }

    /* Step 3: Generate Noise initiation before TCP connect */
    ts2021_handshake_state_t hs_state;
    uint8_t noise_init[101];
    /* Protocol version = CurrentCapabilityVersion from tailcfg (145 as of 2026) */
    err = ts2021_handshake_initiate(&hs_state, noise_init,
                                     s_config.machine_key_priv,
                                     control_key, 145);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "Noise initiate failed: %d", err);
        return err;
    }

    /* Base64-encode initiation for HTTP header */
    char init_b64[160];
    size_t b64_len = tsnode_base64_encode(init_b64, sizeof(init_b64), noise_init, 101);
    TSNODE_LOGI(TAG, "init msg [%02x %02x %02x %02x %02x] b64_len=%zu",
                noise_init[0], noise_init[1], noise_init[2],
                noise_init[3], noise_init[4], b64_len);

    /* Step 4: TCP connect to control plane */
    set_state(TSNODE_CLIENT_HANDSHAKING);
    tsnode_port_socket_t *sock = NULL;
    err = tsnode_port_tcp_connect(&sock, s_config.control_host,
                                   s_config.control_port, 10000);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "TCP connect failed: %d", err);
        return err;
    }

    /* Step 5: HTTP upgrade to ts2021 with X-Tailscale-Handshake header */
    char upgrade_req[512];
    int n = snprintf(upgrade_req, sizeof(upgrade_req),
                     "POST /ts2021 HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: tailscale-control-protocol\r\n"
                     "Connection: upgrade\r\n"
                     "X-Tailscale-Handshake: %s\r\n"
                     "\r\n",
                     s_config.control_host, init_b64);
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
            TSNODE_LOGI(TAG, "upgrade response: %s", (const char *)resp);
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

    /* The HTTP read loop may have already consumed the Noise response
     * bytes along with the HTTP headers. Check for extra bytes. */
    const char *hend = strstr((const char *)resp, "\r\n\r\n");
    size_t hdr_len = (hend != NULL) ? (size_t)(hend - (const char *)resp + 4) : resp_total;
    size_t leftover = resp_total - hdr_len;

    /* Step 6: Read Noise response (51 bytes) and complete handshake */
    uint8_t noise_resp[51];
    size_t resp_read = 0;

    /* If extra bytes were already read from the HTTP loop, use them */
    if (leftover >= 51) {
        memcpy(noise_resp, resp + hdr_len, 51);
        resp_read = 51;
        TSNODE_LOGI(TAG, "Noise response found in HTTP read (%zu extra)", leftover);
        /* Dump extra bytes beyond Noise response to see if server sent more */
        if (leftover > 51) {
            size_t extra = leftover - 51;
            if (extra > 64) extra = 64;
            char hexbuf[133];
            for (size_t i = 0; i < extra; i++) {
                snprintf(hexbuf + i*2, 3, "%02x", (uint8_t)resp[hdr_len + 51 + i]);
            }
            hexbuf[extra*2] = '\0';
            TSNODE_LOGI(TAG, "extra %zu bytes after Noise: %s", extra, hexbuf);
        }
    } else if (leftover > 0) {
        memcpy(noise_resp, resp + hdr_len, leftover);
        resp_read = leftover;
    }

    /* Read remaining bytes from socket */
    while (resp_read < 51) {
        size_t n;
        err = tsnode_port_socket_read(sock, noise_resp + resp_read,
                                       51 - resp_read, &n, 10000);
        if (err != TSNODE_OK || n == 0) break;
        resp_read += n;
    }
    if (err != TSNODE_OK || resp_read != 51) {
        TSNODE_LOGE(TAG, "read Noise response failed: resp_read=%zu err=%d",
                    resp_read, err);
        tsnode_port_socket_close(sock);
        return (err == TSNODE_OK) ? TSNODE_ERR_NETWORK : err;
    }

    err = ts2021_handshake_complete(&s_conn, &hs_state, noise_resp, sock);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "Noise handshake complete failed: %d", err);
        return err;
    }
    TSNODE_LOGI(TAG, "Noise handshake OK");

    /* Prebuffer any leftover bytes from the HTTP read that follow the
     * 51-byte Noise response.  These are server transport frames that
     * arrived in the same TCP segment and must be available to the
     * record layer. */
    if (leftover > 51) {
        ts2021_conn_prebuffer(&s_conn, (const uint8_t *)resp + hdr_len + 51,
                               leftover - 51);
    }

    /* Step 7: Register with auth key */
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
    TSNODE_LOGI(TAG, "sending RegisterRequest (%zu bytes)", reg_len);
    err = ts2021_record_send(&s_conn, (const uint8_t *)reg_buf, reg_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "send RegisterRequest failed: %d", err);
        return err;
    }
    TSNODE_LOGI(TAG, "RegisterRequest sent OK");

    /* Receive RegisterResponse — skip prebuffered server frames that
     * arrived before our RegisterRequest.  Valid RegisterResponse JSON
     * is at least ~50 bytes; smaller frames are post-handshake noise. */
    uint8_t reg_resp[2048];
    size_t reg_resp_len;
    TSNODE_LOGI(TAG, "waiting for RegisterResponse...");
    for (int attempt = 0; attempt < 10; attempt++) {
        err = ts2021_record_recv(&s_conn, reg_resp, sizeof(reg_resp),
                                  &reg_resp_len);
        if (err != TSNODE_OK) {
            TSNODE_LOGE(TAG, "recv RegisterResponse failed: %d", err);
            return err;
        }
        TSNODE_LOGI(TAG, "recv frame #%d: %zu bytes plaintext", attempt, reg_resp_len);
        if (reg_resp_len >= 20) {
            break;  /* Likely a real RegisterResponse */
        }
        TSNODE_LOGW(TAG, "frame too small (%zu bytes), skipping", reg_resp_len);
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

    /* Task self-deletes */
    vTaskDelete(NULL);
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

    /* Create task via port (40KB stack for mbedTLS + crypto, priority 5) */
    tsnode_err_t err = tsnode_port_task_create(client_task, NULL,
                                                "tsclient", 40960, 5);
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
