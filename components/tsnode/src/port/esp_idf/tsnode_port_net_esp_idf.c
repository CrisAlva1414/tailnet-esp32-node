/*
 * Implementación ESP-IDF del port de tsnode — networking (ADR-0008).
 *
 * TCP vía lwIP, TLS vía mbedTLS. Único lugar del componente donde se
 * permiten headers de plataforma.
 */

#include <string.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"

#include "tsnode_port.h"

static const char *TAG = "tsnode_port_net";

/* Global DRBG state: entropy source + CTR_DRBG. Initialized once. */
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool s_rng_initialized;

static int init_rng(void)
{
    if (s_rng_initialized) {
        return 0;
    }
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);
    int ret = mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func,
                                     &s_entropy,
                                     (const unsigned char *)"tsnode", 6);
    if (ret != 0) {
        ESP_LOGE(TAG, "ctr_drbg_seed: -0x%04x", -ret);
        return ret;
    }
    s_rng_initialized = true;
    return 0;
}

/* Socket opaco: contends TCP (mbedTLS net) + TLS context */
struct tsnode_port_socket {
    mbedtls_net_context      net;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         ca_certs;
    bool                     use_tls;
};

/* Convert mbedTLS error to tsnode_err_t */
static tsnode_err_t tls_err_to_tsn(int ret)
{
    if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
        return TSNODE_ERR_TIMEOUT;
    }
    return TSNODE_ERR_NETWORK;
}

tsnode_err_t tsnode_port_tcp_connect(tsnode_port_socket_t **out_sock,
                                     const char *host, uint16_t port,
                                     uint32_t timeout_ms)
{
    if (out_sock == NULL || host == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    tsnode_port_socket_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return TSNODE_ERR_NO_MEMORY;
    }

    mbedtls_net_init(&s->net);

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int ret = mbedtls_net_connect(&s->net, host, port_str,
                                  MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        ESP_LOGE(TAG, "tcp_connect to %s:%u failed: -0x%04x", host, port, -ret);
        mbedtls_net_free(&s->net);
        free(s);
        return TSNODE_ERR_NETWORK;
    }

    s->use_tls = false;
    *out_sock = s;
    return TSNODE_OK;
}

tsnode_err_t tsnode_port_tls_connect(tsnode_port_socket_t **out_sock,
                                     const char *host, uint16_t port,
                                     uint32_t timeout_ms)
{
    if (out_sock == NULL || host == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    tsnode_port_socket_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return TSNODE_ERR_NO_MEMORY;
    }

    /* Ensure RNG is initialized */
    int ret = init_rng();
    if (ret != 0) {
        free(s);
        return TSNODE_ERR_CRYPTO;
    }

    mbedtls_net_init(&s->net);
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca_certs);

    /* Configure TLS */
    ret = mbedtls_ssl_config_defaults(&s->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        ESP_LOGE(TAG, "ssl_config_defaults: -0x%04x", -ret);
        goto fail;
    }

    /* Load system CA bundle (must be after ssl_config_defaults) */
    ret = esp_crt_bundle_attach(&s->conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "crt_bundle_attach failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random,
                         &s_ctr_drbg);

    ret = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (ret != 0) {
        ESP_LOGE(TAG, "ssl_setup: -0x%04x", -ret);
        goto fail;
    }

    mbedtls_ssl_set_hostname(&s->ssl, host);

    /* TCP connect */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    ret = mbedtls_net_connect(&s->net, host, port_str,
                              MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        ESP_LOGE(TAG, "tls tcp_connect to %s:%u failed: -0x%04x",
                 host, port, -ret);
        goto fail;
    }

    mbedtls_ssl_set_bio(&s->ssl, &s->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    /* TLS handshake with timeout */
    if (timeout_ms > 0) {
        uint64_t deadline;
        tsnode_port_uptime_ms(&deadline);
        deadline += timeout_ms;
        mbedtls_ssl_conf_read_timeout(&s->conf, (int)timeout_ms);
    }

    while ((ret = mbedtls_ssl_handshake(&s->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "ssl_handshake: -0x%04x", -ret);
            goto fail;
        }
    }

    /* Verify certificate */
    uint32_t flags = mbedtls_ssl_get_verify_result(&s->ssl);
    if (flags != 0) {
        char vbuf[512];
        mbedtls_x509_crt_verify_info(vbuf, sizeof(vbuf), "  ! ", flags);
        ESP_LOGE(TAG, "cert verify failed:\n%s", vbuf);
        goto fail;
    }

    s->use_tls = true;
    *out_sock = s;
    return TSNODE_OK;

fail:
    tsnode_port_socket_close(s);
    return TSNODE_ERR_NETWORK;
}

tsnode_err_t tsnode_port_socket_write(tsnode_port_socket_t *sock,
                                      const uint8_t *data, size_t nlen,
                                      uint32_t timeout_ms)
{
    if (sock == NULL || data == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    size_t written = 0;
    while (written < nlen) {
        int ret;
        if (sock->use_tls) {
            ret = mbedtls_ssl_write(&sock->ssl, data + written, nlen - written);
        } else {
            ret = mbedtls_net_send(&sock->net, data + written, nlen - written);
        }
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
                   ret == MBEDTLS_ERR_NET_SEND_FAILED) {
            continue;
        } else {
            return TSNODE_ERR_NETWORK;
        }
    }
    return TSNODE_OK;
}

tsnode_err_t tsnode_port_socket_read(tsnode_port_socket_t *sock,
                                     uint8_t *buf, size_t buf_size,
                                     size_t *nread, uint32_t timeout_ms)
{
    if (sock == NULL || buf == NULL || nread == NULL) {
        return TSNODE_ERR_INVALID_ARG;
    }

    int ret;
    if (sock->use_tls) {
        ret = mbedtls_ssl_read(&sock->ssl, buf, buf_size);
    } else {
        ret = mbedtls_net_recv(&sock->net, buf, buf_size);
    }

    if (ret > 0) {
        *nread = (size_t)ret;
        return TSNODE_OK;
    }
    if (ret == 0) {
        /* Connection closed */
        *nread = 0;
        return TSNODE_ERR_NETWORK;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
        ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        *nread = 0;
        return TSNODE_ERR_TIMEOUT;
    }
    *nread = 0;
    return tls_err_to_tsn(ret);
}

void tsnode_port_socket_close(tsnode_port_socket_t *sock)
{
    if (sock == NULL) {
        return;
    }
    if (sock->use_tls) {
        mbedtls_ssl_close_notify(&sock->ssl);
        mbedtls_ssl_free(&sock->ssl);
        mbedtls_ssl_config_free(&sock->conf);
        mbedtls_x509_crt_free(&sock->ca_certs);
    }
    mbedtls_net_free(&sock->net);
    free(sock);
}
