# Guía de Integración — tsnode como librería reutilizable

## Arquitectura

```
┌─────────────────────────────────────────┐
│  App (main/)                            │
│  tsnode_simple.c, console, display      │
├─────────────────────────────────────────┤
│  Public API: tsnode.h                   │
│  tsnode_init(), tsnode_start(), ...     │
├─────────────────────────────────────────┤
│  Core (src/) — C puro, sin plataforma   │
│  proto/: ts2021, h2, client, map, reg   │
│  wg/: wg.c, replay.c                   │
│  crypto/: blake2s, hmac, base64         │
├─────────────────────────────────────────┤
│  Port Layer: tsnode_port.h              │
│  16 funciones abstraction               │
├─────────────────────────────────────────┤
│  Platform: port/esp_idf/               │
│  RNG, uptime, tasks, TCP/TLS, UDP, KV  │
└─────────────────────────────────────────┘
```

## Dependencias del componente

```
REQUIRES: esp_hw_support  esp_timer  mbedtls  esp_netif  nvs_flash  lwip
```

## Port Layer — qué implementar por plataforma

El port layer define 16 funciones en `src/port/tsnode_port.h`:

### RNG
```c
tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len);
```
Linux: `/dev/urandom`. STM32: hardware RNG peripheral.

### Tiempo
```c
tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms);
void tsnode_port_delay_ms(uint32_t ms);
```
Linux: `clock_gettime(CLOCK_MONOTONIC)`, `usleep()`. STM32: SysTick.

### Tasks
```c
tsnode_err_t tsnode_port_task_create(tsnode_port_task_fn fn, void *arg,
                                     const char *name, size_t stack_bytes,
                                     int priority);
void tsnode_port_task_delete_self(void);
```
Linux: pthreads. STM32: FreeRTOS o bare-metal main loop.

### Logging
```c
void tsnode_port_set_log(tsnode_port_log_fn fn);
tsnode_port_log_fn tsnode_port_get_log(void);
```
Linux: `fprintf(stderr, ...)`. STM32: UART/SWO.

### TCP/TLS
```c
tsnode_err_t tsnode_port_tcp_connect(tsnode_port_socket_t **out_sock,
                                     const char *host, uint16_t port,
                                     uint32_t timeout_ms);
tsnode_err_t tsnode_port_tls_connect(tsnode_port_socket_t **out_sock,
                                     const char *host, uint16_t port,
                                     uint32_t timeout_ms);
tsnode_err_t tsnode_port_socket_write(tsnode_port_socket_t *sock,
                                      const uint8_t *data, size_t nlen,
                                      uint32_t timeout_ms);
tsnode_err_t tsnode_port_socket_read(tsnode_port_socket_t *sock,
                                     uint8_t *buf, size_t buf_size,
                                     size_t *nread, uint32_t timeout_ms);
void tsnode_port_socket_close(tsnode_port_socket_t *sock);
```
Linux: POSIX sockets + OpenSSL/mbedTLS. STM32: lwIP + mbedTLS.

### UDP (WireGuard data plane)
```c
tsnode_err_t tsnode_port_udp_bind(tsnode_port_udp_socket_t **out_sock,
                                  uint16_t port);
tsnode_err_t tsnode_port_udp_sendto(tsnode_port_udp_socket_t *sock,
                                    const uint8_t *data, size_t len,
                                    uint32_t dest_ip, uint16_t dest_port);
tsnode_err_t tsnode_port_udp_recvfrom(tsnode_port_udp_socket_t *sock,
                                      uint8_t *buf, size_t buf_size,
                                      size_t *nread,
                                      uint32_t *src_ip, uint16_t *src_port,
                                      uint32_t timeout_ms);
void tsnode_port_udp_close(tsnode_port_udp_socket_t *sock);
```
Linux: POSIX `sendto`/`recvfrom`. STM32: lwIP.

### KV Storage
```c
bool tsnode_port_kv_get(const char *ns, const char *key, uint8_t *out,
                        size_t len);
bool tsnode_port_kv_set(const char *ns, const char *key, const uint8_t *val,
                        size_t len);
void tsnode_port_kv_del(const char *ns, const char *key);
```
Linux: archivos. STM32: NVS o SPI flash + wear-leveling.

## Módulos portables (sin cambios)

| Módulo | Archivos | Notas |
|--------|----------|-------|
| WireGuard core | `wg/wg.c`, `wg/replay.c` | Crypto inyectable via vtable |
| BLAKE2s | `crypto/blake2s.c` | RFC 7693, test vectors |
| HMAC/HKDF | `crypto/hmac_blake2s.c` | WireGuard KDF compatible |
| HTTP/2 | `proto/h2.c` | I/O inyectada |
| Map/Register | `proto/tsnode_map.c`, `tsnode_register.c` | JSON puro |

## Módulos que necesitan refactor para portabilidad

### ts2021.c (Noise/ts2021)

**Problema**: llama `mbedtls_chachapoly_*` y `x25519_wrapper.h` directamente.

**Solución**: crear `ts2021_crypto_t` vtable (similar a `tsnode_wg_crypto_t`):

```c
typedef struct {
    tsnode_err_t (*dh)(uint8_t shared[32], const uint8_t priv[32],
                       const uint8_t pub[32]);
    tsnode_err_t (*pubkey)(uint8_t pub[32], const uint8_t priv[32]);
    tsnode_err_t (*aead_seal)(uint8_t *out, const uint8_t key[32],
                              const uint8_t nonce[12], const uint8_t *aad,
                              size_t aad_len, const uint8_t *pt, size_t pt_len);
    tsnode_err_t (*aead_open)(uint8_t *out, const uint8_t key[32],
                              const uint8_t nonce[12], const uint8_t *aad,
                              size_t aad_len, const uint8_t *ct, size_t ct_len);
    tsnode_err_t (*random)(uint8_t *out, size_t len);
} ts2021_crypto_t;
```

Esfuerzo: **~200 líneas** de refactor.

## Ejemplo: port para Linux

```c
/* port/linux/tsnode_port_linux.c */
#include "tsnode_port.h"
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return TSNODE_ERR_CRYPTO;
    size_t r = fread(out, 1, len, f);
    fclose(f);
    return r == len ? TSNODE_OK : TSNODE_ERR_CRYPTO;
}

tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *out_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return TSNODE_OK;
}

void tsnode_port_delay_ms(uint32_t ms) {
    usleep(ms * 1000);
}

/* ... más funciones del port layer ... */
```

## Estructura de archivos para un proyecto nuevo

```
tu-proyecto/
├── components/
│   └── tsnode/                    # copiado de este repo
│       ├── include/
│       │   ├── tsnode.h
│       │   ├── tsnode_err.h
│       │   └── tsnode_config.h
│       └── src/
│           ├── proto/
│           ├── wg/
│           ├── crypto/
│           └── port/
│               └── linux/         # tu port layer
├── main/
│   ├── main.c                     # usa tsnode_simple pattern
│   ├── tsnode_simple.c            # copiado de este repo
│   └── tsnode_simple.h
└── sdkconfig.defaults
```

## Checklist de seguridad para producción

- [ ] Flash encryption habilitado (mode Release)
- [ ] NVS encryption habilitado
- [ ] Secure Boot v2 evaluado
- [ ] eFuses de debug deshabilitados
- [ ] Auth keys: one-time, expiry corto, tags apropiados
- [ ] ACLs de Tailscale: restricciones explícitas para `tag:esp32-iot`
- [ ] Logs: sin claves, tokens, o IPs en DEBUG level
