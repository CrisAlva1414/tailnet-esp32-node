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
#include "h2.h"
#include "tsnode_map.h"
#include "tsnode_port.h"
#include "tsnode_register.h"
#include "x25519_wrapper.h"
#include "base64.h"

#define TAG "tsnode_client"

#define CONTROL_KEY_BUF_SIZE 1024
#define REGISTER_BUF_SIZE    1024
/* Respuestas HTTP/2 completas (headers h2 ya removidos). RegisterResponse
 * ronda 600 B; 4 KiB es margen holgado (ADR-0009 D3). */
#define REGISTER_RESPONSE_BUF_SIZE 4096
#define MAP_REQUEST_BUF_SIZE 1024
/* MapResponse observado ~17 KB con tailnet chica; 32 KiB estático cubre
 * crecimiento moderado de peers sin heap (ADR-0009 D3). */
#define MAP_RESPONSE_BUF_SIZE 32768

static tsnode_client_state_t s_state = TSNODE_CLIENT_IDLE;
static tsnode_client_config_t s_config;
static ts2021_conn_t s_conn;
static uint8_t s_node_key[32];       /* WireGuard identity key (privada) */
static uint8_t s_node_key_pub[32];   /* pública, para header ts-lb */
static h2_conn_t s_h2;

/* Buffers grandes fuera del stack: la tarea corre con 40 KB y el pico de
 * crypto (mbedTLS) ya usa buena parte; 32 KB en stack es crash seguro. */
static uint8_t s_reg_resp[REGISTER_RESPONSE_BUF_SIZE];
static uint8_t s_map_resp[MAP_RESPONSE_BUF_SIZE];

/* ---- I/O callbacks para h2 sobre la capa de registros ts2021 ---- */

static tsnode_err_t h2_io_send(void *ctx, const uint8_t *data, size_t len)
{
    return ts2021_record_send((ts2021_conn_t *)ctx, data, len);
}

static tsnode_err_t h2_io_recv(void *ctx, uint8_t *buf, size_t cap,
                                size_t *out_len)
{
    /* Registros Noise de longitud cero son legales (padding del par):
     * se consumen acá para que h2 interprete 0 únicamente como EOF. */
    ts2021_conn_t *conn = (ts2021_conn_t *)ctx;
    do {
        tsnode_err_t err = ts2021_record_recv(conn, buf, cap, out_len);
        if (err != TSNODE_OK) return err;
    } while (*out_len == 0);
    return TSNODE_OK;
}

static void bytes_to_hex_str(char out[65], const uint8_t in[32])
{
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", in[i]);
    }
    out[64] = '\0';
}

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
    if (tsnode_x25519_keygen(s_node_key, s_node_key_pub) != 0) {
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

    /* Consume EarlyNoise payload (if present).
     *
     * After the Noise handshake the server may push an "early payload"
     * before any HTTP/2 communication.  Format (from conn.go):
     *   5 bytes: magic "\xff\xff\xffTS"
     *   4 bytes: payload length (big-endian uint32)
     *   N bytes: JSON (e.g. {"nodeKeyChallenge":"chalpub:..."})
     *
     * The Go client's ts2021.Conn.readHeader() silently consumes this.
     * The NodeKeyChallenge content is never used by the Go client, so we
     * simply skip it.
     *
     * The magic and length may span multiple Noise transport records,
     * so we accumulate bytes until we can check for the magic prefix. */
    {
        /* Accumulate at least 9 bytes (magic + length header) from the
         * first Noise record(s).  We keep a small ring buffer for any
         * bytes beyond the header that arrived in the same record. */
        uint8_t early_hdr[9];
        size_t hdr_have = 0;
        uint8_t extra[512];    /* bytes beyond the 9-byte header */
        size_t extra_len = 0;
        bool is_early = false;

        while (hdr_have < 9) {
            uint8_t frame[512];
            size_t frame_len;
            err = ts2021_record_recv(&s_conn, frame, sizeof(frame), &frame_len);
            if (err != TSNODE_OK) {
                TSNODE_LOGE(TAG, "recv post-handshake frame failed: %d", err);
                tsnode_port_socket_close(sock);
                return err;
            }
            TSNODE_LOGI(TAG, "post-handshake frame: %zu bytes", frame_len);

            /* How many bytes do we need to fill the header? */
            size_t need = 9 - hdr_have;
            size_t take = (frame_len < need) ? frame_len : need;
            memcpy(early_hdr + hdr_have, frame, take);
            hdr_have += take;

            /* Any remaining bytes in this frame are payload data */
            if (frame_len > take) {
                size_t rem = frame_len - take;
                if (rem <= sizeof(extra) - extra_len) {
                    memcpy(extra + extra_len, frame + take, rem);
                    extra_len += rem;
                }
            }

            /* Check magic once we have at least 5 bytes */
            if (hdr_have >= 5 && !is_early) {
                if (early_hdr[0] == 0xff && early_hdr[1] == 0xff &&
                    early_hdr[2] == 0xff && early_hdr[3] == 'T' &&
                    early_hdr[4] == 'S') {
                    is_early = true;
                    TSNODE_LOGI(TAG, "EarlyNoise magic detected");
                } else {
                    /* Not early payload — this is likely an HTTP/2 SETTINGS
                     * frame or other post-handshake data.  We don't speak
                     * HTTP/2, so just skip it. */
                    TSNODE_LOGI(TAG, "non-EarlyNoise post-handshake frame "
                                 "(%zu bytes), skipping", frame_len);
                    break;
                }
            }
        }

        if (is_early && hdr_have == 9) {
            /* Parse payload length from bytes 5-8 */
            uint32_t ep_len = ((uint32_t)early_hdr[5] << 24) |
                              ((uint32_t)early_hdr[6] << 16) |
                              ((uint32_t)early_hdr[7] << 8)  |
                              ((uint32_t)early_hdr[8]);
            TSNODE_LOGI(TAG, "EarlyNoise payload: %u bytes", ep_len);

            /* We already have extra_len bytes of the JSON payload.
             * Drain more records until we've consumed ep_len bytes total. */
            size_t consumed = extra_len;
            while (consumed < ep_len) {
                uint8_t skip[256];
                size_t skip_len;
                err = ts2021_record_recv(&s_conn, skip, sizeof(skip), &skip_len);
                if (err != TSNODE_OK) break;
                consumed += skip_len;
            }
            TSNODE_LOGI(TAG, "EarlyNoise skipped (%u bytes, consumed %zu)",
                         ep_len, consumed);
        }
    }

    /* Step 7: HTTP/2 sobre el túnel Noise (ADR-0009).
     * Sin esta capa el server descarta la conexión con "bogus greeting":
     * verificado empíricamente contra controlplane.tailscale.com
     * (sesión 2026-08-23). El SETTINGS del server ya puede estar prebuffered;
     * h2_client_start lo consume del record layer. */
    h2_io_t h2io = {
        .ctx = &s_conn,
        .send_bytes = h2_io_send,
        .recv_record = h2_io_recv,
    };
    err = h2_client_start(&s_h2, &h2io);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "http2 start failed: %d", err);
        return err;
    }
    TSNODE_LOGI(TAG, "http2 over noise OK");

    /* Step 8: Register via POST /machine/register (h2). El header Ts-Lb
     * lleva nuestra node key pública (control/tsp/register.go). */
    set_state(TSNODE_CLIENT_REGISTERING);
    char reg_req[REGISTER_BUF_SIZE];
    size_t reg_len;
    err = tsnode_register_build_request(reg_req, sizeof(reg_req), &reg_len,
                                         s_node_key,
                                         s_config.machine_key_priv,
                                         s_config.auth_key,
                                         s_config.hostname,
                                         145); /* CurrentCapabilityVersion */
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "build RegisterRequest failed: %d", err);
        return err;
    }

    char node_key_hex[65];
    bytes_to_hex_str(node_key_hex, s_node_key_pub);
    char lb_value[8 + 64 + 1]; /* "nodekey:" + hex + NUL */
    snprintf(lb_value, sizeof(lb_value), "nodekey:%s", node_key_hex);

    size_t reg_resp_len;
    err = h2_post(&s_h2, s_config.control_host, "/machine/register",
                  lb_value, (const uint8_t *)reg_req, reg_len,
                  s_reg_resp, sizeof(s_reg_resp) - 1, &reg_resp_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "register POST failed: %d", err);
        return err;
    }
    /* -1 arriba garantiza lugar para el NUL que exige el parser. */
    s_reg_resp[reg_resp_len] = '\0';
    TSNODE_LOGI(TAG, "RegisterResponse (%zu bytes)", reg_resp_len);

    tsnode_register_response_t reg_result;
    err = tsnode_register_parse_response(&reg_result,
                                          (const char *)s_reg_resp,
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
        /* El control plane pide login interactivo: la auth key no cubrió
         * el registro (expirada, reusada, o ACL). Es un fallo de
         * provisioning, no de protocolo. La URL no contiene secretos. */
        TSNODE_LOGE(TAG, "interactive auth required: %s",
                    reg_result.auth_url);
        return TSNODE_ERR_PROVISIONING;
    }

    if (!reg_result.machine_authorized) {
        /* Auth key aceptada pero device approval pendiente en la tailnet:
         * el nodo queda registrado esperando aprobación humana. */
        TSNODE_LOGW(TAG, "machine not yet authorized (device approval)");
    }
    TSNODE_LOGI(TAG, "registration done");

    /* Step 9: Map sync via POST /machine/map (h2). */
    set_state(TSNODE_CLIENT_MAP_SYNC);
    uint8_t zero_disco[32] = {0};
    char map_req[MAP_REQUEST_BUF_SIZE];
    size_t map_req_len;
    err = tsnode_map_build_request(map_req, sizeof(map_req), &map_req_len,
                                    s_node_key,
                                    zero_disco,
                                    s_config.hostname,
                                    145, false);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "build MapRequest failed: %d", err);
        return err;
    }

    size_t map_wire_len;
    err = h2_post(&s_h2, s_config.control_host, "/machine/map",
                  lb_value, (const uint8_t *)map_req, map_req_len,
                  s_map_resp, sizeof(s_map_resp) - 1, &map_wire_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "map POST failed: %d", err);
        return err;
    }
    s_map_resp[map_wire_len] = '\0';
    TSNODE_LOGI(TAG, "MapResponse (%zu bytes wire)", map_wire_len);

    const uint8_t *map_json = NULL;
    size_t map_json_len = 0;
    err = tsnode_map_parse_framed(s_map_resp, map_wire_len,
                                   &map_json, &map_json_len);
    if (err != TSNODE_OK) {
        TSNODE_LOGE(TAG, "parse MapResponse framing failed: %d", err);
        return err;
    }

    tsnode_map_netmap_t netmap;
    err = tsnode_map_parse_response(&netmap, (const char *)map_json,
                                     map_json_len);
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

    /* Task self-deletes via port (ADR-0006) */
    tsnode_port_task_delete_self();
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
