/*
 * Netmap: MapRequest/MapResponse (ADR-0008).
 *
 * Minimal parser — extracts only what the ESP32 data plane needs.
 * No heap allocation. Scans JSON linearly for known field patterns.
 *
 * C puro: sin headers de plataforma (ADR-0006). Sin logging: el caller
 * agrega contexto con los resultados parseados (los JSON de respuesta
 * pueden contener datos de la tailnet que no deben ir a log por defecto).
 */

#include "tsnode_map.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Hex decoding ---- */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_to_bytes(uint8_t *out, size_t out_len, const char *hex,
                        size_t hex_len)
{
    if (hex_len != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_digit(hex[i * 2]);
        int lo = hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* ---- MapRequest builder ---- */

tsnode_err_t tsnode_map_build_request(char *buf, size_t buf_size,
                                      size_t *out_len,
                                      const uint8_t node_key[32],
                                      const uint8_t disco_key[32],
                                      const char *hostname,
                                      uint32_t capability_version,
                                      bool stream)
{
    if (buf == NULL || buf_size == 0 || out_len == NULL || node_key == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    char nk_hex[65], dk_hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(nk_hex + i * 2, 3, "%02x", node_key[i]);
        snprintf(dk_hex + i * 2, 3, "%02x", disco_key[i]);
    }
    nk_hex[64] = '\0';
    dk_hex[64] = '\0';

    size_t pos = 0;
    int n;

    buf[pos++] = '{';

    n = snprintf(buf + pos, buf_size - pos, "\"Version\":%" PRIu32, capability_version);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    buf[pos++] = ',';
    n = snprintf(buf + pos, buf_size - pos,
                 "\"NodeKey\":\"nodekey:%s\"", nk_hex);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    buf[pos++] = ',';
    n = snprintf(buf + pos, buf_size - pos,
                 "\"DiscoKey\":\"discokey:%s\"", dk_hex);
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    buf[pos++] = ',';
    n = snprintf(buf + pos, buf_size - pos,
                 "\"Stream\":%s", stream ? "true" : "false");
    if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
    pos += n;

    if (hostname != NULL && hostname[0] != '\0') {
        buf[pos++] = ',';
        n = snprintf(buf + pos, buf_size - pos,
                     "\"Hostinfo\":{\"Hostname\":\"%s\"}", hostname);
        if (n < 0 || (size_t)n >= buf_size - pos) return TSNODE_ERR_NO_MEMORY;
        pos += n;
    }

    buf[pos++] = '}';
    buf[pos] = '\0';

    *out_len = pos;
    return TSNODE_OK;
}

/* ---- MapResponse framing (control/tsp/map.go) ---- */

/* Firma de frames zstd: pedimos JSON crudo (sin Compress), verlo acá
 * significa que el par violó lo acordado o que cambió el protocolo. */
static const uint8_t ZSTD_MAGIC[4] = {0x28, 0xB5, 0x2F, 0xFD};

tsnode_err_t tsnode_map_parse_framed(const uint8_t *wire, size_t wire_len,
                                      const uint8_t **json_out,
                                      size_t *json_len_out)
{
    if (wire == NULL || json_out == NULL || json_len_out == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }
    if (wire_len < 4) {
        return TSNODE_ERR_NETWORK;
    }

    uint32_t declared = (uint32_t)wire[0] |
                        ((uint32_t)wire[1] << 8) |
                        ((uint32_t)wire[2] << 16) |
                        ((uint32_t)wire[3] << 24);

    /* Input hostil por defecto: el length declarado nunca se usa para
     * indexar; solo se valida contra el tamaño real recibido. */
    if ((size_t)declared != wire_len - 4) {
        return TSNODE_ERR_NETWORK;
    }

    if (declared >= sizeof(ZSTD_MAGIC) &&
        memcmp(wire + 4, ZSTD_MAGIC, sizeof(ZSTD_MAGIC)) == 0) {
        return TSNODE_ERR_NOT_IMPLEMENTED;
    }

    *json_out = wire + 4;
    *json_len_out = declared;
    return TSNODE_OK;
}

/* ---- MapResponse parser ---- */

/* Find a string value for a JSON key. Returns pointer past closing quote,
 * or NULL if not found. Does NOT handle escaped quotes. */
static const char *find_json_string(const char *json, const char *key,
                                    char *value_out, size_t value_out_len)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *found = strstr(json, pattern);
    if (found == NULL) return NULL;

    found += strlen(pattern);
    /* Skip whitespace and colon */
    while (*found == ' ' || *found == ':') found++;
    if (*found != '"') return NULL;
    found++; /* skip opening quote */

    const char *end = strchr(found, '"');
    if (end == NULL) return NULL;

    size_t len = (size_t)(end - found);
    if (len >= value_out_len) len = value_out_len - 1;
    memcpy(value_out, found, len);
    value_out[len] = '\0';

    return end + 1;
}

/* Find next occurrence of "Key":"nodekey:..." and extract the 32-byte key */
static const char *find_next_node_key(const char *search_from,
                                      uint8_t key_out[32])
{
    const char *marker = "\"Key\":\"nodekey:";
    const char *found = strstr(search_from, marker);
    if (found == NULL) return NULL;

    found += strlen(marker);
    if (strlen(found) < 64) return NULL;

    if (hex_to_bytes(key_out, 32, found, 64) != 0) {
        return NULL;
    }
    return found + 64;
}

tsnode_err_t tsnode_map_parse_response(tsnode_map_netmap_t *netmap,
                                        const char *json, size_t json_len)
{
    if (netmap == NULL || json == NULL || json_len == 0) {
        return TSNODE_ERR_INVALID_ARG;
    }
    /* El escaneo usa strstr/strchr: exige buffer NUL-terminado (el caller
     * garantiza espacio para el NUL al pedir la respuesta a h2). json_len
     * valida no-vacío acá; los límites reales los pone el NUL. */

    memset(netmap, 0, sizeof(*netmap));

    /* Extract Self.Key */
    find_next_node_key(json, netmap->self_node_key);

    /* Extract self IP from Self.PrimaryRoutes or AllowedIPs.
     * In practice, the node's own IP is in the first peer entry
     * with matching key, or we look for "100." prefix in AllowedIPs.
     * For simplicity, scan for first "100." occurrence. */
    const char *ip_marker = "\"100.";
    const char *ip_found = strstr(json, ip_marker);
    if (ip_found != NULL) {
        /* Go back to find opening quote */
        const char *start = ip_found;
        while (start > json && *(start - 1) != '"') start--;
        size_t len = 0;
        const char *p = start;
        while (*p && *p != '"' && len < sizeof(netmap->self_ip) - 1) {
            netmap->self_ip[len++] = *p++;
        }
        netmap->self_ip[len] = '\0';
    }

    /* Parse Peers array */
    const char *peers_marker = "\"Peers\":[";
    const char *peers_start = strstr(json, peers_marker);
    if (peers_start != NULL) {
        peers_start += strlen(peers_marker);

        /* Scan for peer entries */
        const char *scan = peers_start;
        while (scan != NULL && netmap->peer_count < TSNODE_MAP_MAX_PEERS) {
            /* Find next peer object start */
            const char *peer_start = strchr(scan, '{');
            if (peer_start == NULL) break;
            scan = peer_start + 1;

            /* Check if we've exited the Peers array */
            if (*peer_start == ']' || peer_start >= peers_start + strlen(peers_start) - 1) {
                break;
            }

            tsnode_map_peer_t *peer = &netmap->peers[netmap->peer_count];

            /* Find peer Key */
            const char *key_end = find_next_node_key(peer_start, peer->key);
            if (key_end == NULL) continue;

            /* Find Hostname (in HostInfo sub-object) */
            find_json_string(peer_start, "Hostname",
                            peer->host_name, sizeof(peer->host_name));

            /* Find AllowedIPs — look for "100." inside AllowedIPs array */
            const char *allowed = strstr(peer_start, "\"AllowedIPs\"");
            if (allowed != NULL) {
                const char *ip = strstr(allowed, "\"100.");
                if (ip != NULL) {
                    ip++; /* skip quote */
                    size_t iplen = 0;
                    while (*ip && *ip != '"' && *ip != '/' &&
                           iplen < sizeof(peer->tailscale_ip) - 1) {
                        peer->tailscale_ip[iplen++] = *ip++;
                    }
                    peer->tailscale_ip[iplen] = '\0';
                }
            }

            /* Find Endpoints */
            const char *ep = strstr(peer_start, "\"Endpoints\"");
            if (ep != NULL) {
                /* Extract port from first endpoint string */
                const char *ep_str = strchr(ep, '"');
                if (ep_str != NULL) {
                    ep_str++;
                    const char *colon = strchr(ep_str, ':');
                    if (colon != NULL) {
                        peer->listen_port = (uint16_t)atoi(colon + 1);
                    }
                }
            }

            peer->online = (peer->listen_port > 0);
            netmap->peer_count++;

            /* Move past this peer object */
            scan = key_end;
        }
    }

    return TSNODE_OK;
}
