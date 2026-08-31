# How to Use tsnode — Guía para AI Agents

> Esta guía es para AI coding assistants (opencode, Cursor, Copilot, etc.)
> que trabajen en proyectos que usan o consumen tsnode.
> Para reglas completas del repo, ver `AGENTS.md`.

## Resumen ejecutivo

tsnode es un **cliente Tailscale mínimo en C para ESP32**. La librería
vive en `components/tsnode/` y se consume como componente ESP-IDF.

**Regla #1**: el core (`src/`) es C puro sin headers de plataforma.
El acceso a ESP-IDF es solo via port layer (`src/port/`).

## Estructura del repo

```
├── AGENTS.md                    # Reglas completas (LEER PRIMERO)
├── components/tsnode/           # La librería reutilizable
│   ├── include/                 # API pública (tsnode.h, tsnode_err.h)
│   └── src/
│       ├── proto/               # Protocolo: ts2021, h2, client, map, register
│       ├── wg/                  # WireGuard core (wg.c, replay.c)
│       ├── crypto/              # BLAKE2s, HMAC, base64
│       └── port/                # Abstraction layer
│           ├── tsnode_port.h    # Interface (16 funciones)
│           └── esp_idf/         # Implementación ESP-IDF
├── main/                        # App de referencia (bank of tests)
├── tests/                       # Tests host (unit/)
└── docs/                        # ADRs, sesiones, guías
```

## Reglas de arquitectura (resumen de ADR-0005/0006)

### Capas

| Capa | Ubicación | Puede importar |
|------|-----------|----------------|
| App | `main/` | Todo (tsnode, ESP-IDF, FreeRTOS) |
| Public API | `include/tsnode.h` | Nada de plataforma |
| Core | `src/proto/`, `src/wg/`, `src/crypto/` | Solo `tsnode_port.h` |
| Port | `src/port/tsnode_port.h` | Libre |
| Platform | `src/port/esp_idf/` | ESP-IDF completo |

### Regla dura

**NUNCA** agregar `#include` de ESP-IDF/FreeRTOS en archivos bajo `src/proto/`,
`src/wg/`, o `src/crypto/`. Si necesitás algo de plataforma, va al port layer.

## Qué modificar para cada tarea

### Agregar nueva feature de protocolo

Ubicación: `src/proto/`
Archivos típicos: crear `src/proto/nuevo_protocolo.c` + `.h`
Regla: C puro, sin headers de plataforma, I/O inyectada via callbacks.

### Agregar nuevo peer type o funcionalidad WG

Ubicación: `src/wg/`
Archivos: `wg.c`, `wg.h`
Regla: crypto inyectable via `tsnode_wg_crypto_t` vtable.

### Cambiar plataforma (Linux, STM32, etc.)

Ubicación: `src/port/<nueva_plataforma>/`
Archivos: implementar las 16 funciones de `tsnode_port.h`
Regla: una función por función, no omitir nada.

### Agregar comando de consola

Ubicación: `main/console.c`
Regla: la consola es app-layer, no pertenece al componente.

### Modificar el display

**No hacer en este repo.** El display está pendiente para otro proyecto.

### Modificar la API pública

Ubicación: `include/tsnode.h`
Regla:任何cambio rompe compatibilidad. Documentar en ADR si es necesario.

## Dependencias

### El componente tsnode depende de:

```
esp_hw_support  esp_timer  mbedtls  esp_netif  nvs_flash  lwip
```

### El componente NO puede importar directamente:

- FreeRTOS (excepto via port layer)
- `esp_log.h` (excepto via port layer)
- Cualquier header de aplicación

## Tests

### Correr tests host

```bash
cd tests/unit
make clean && make check
```

### Tests disponibles

| Test | Qué cubre | Archivos |
|------|-----------|----------|
| `test_h2` | HTTP/2 + HPACK + MapResponse framing | 15 tests |
| `test_blake2s` | BLAKE2s hash (RFC 7693 vectors) | ~20 tests |
| `test_replay` | Anti-replay filter | ~20 tests |
| `test_wg` | WireGuard core completo | ~70 tests |

### Agregar nuevo test

1. Crear `tests/unit/test_nuevo.c`
2. Agregar target en `tests/unit/Makefile`
3. Agregar a la lista `TESTS`
4. Correr `make check`

## Convenciones de código

- **Estándar**: C11 con dialecto GNU (`-std=gnu11`)
- **Flags**: `-Wall -Wextra -Werror` (firmware), `-Wpedantic` (tests host)
- **Naming**: `snake_case` para funciones/variables, `UPPER_CASE` para macros
- **Headers**: include guards `#ifndef TSNODE_*_H`
- **Errores**: toda función retorna `tsnode_err_t`, se revisa en call site
- **Buffers**: estáticos con tamaño máximo en compile time (sin heap en hot path)
- **Constant-time**: comparación de MACs/claves siempre via comparación constante

## Qué NO hacer

| Anti-patrón | Por qué |
|-------------|---------|
| `#include "esp_log.h"` en `src/proto/` | Rompe portabilidad (ADR-0006) |
| `vTaskDelay()` en `src/proto/` | Usar `tsnode_port_delay_ms()` |
| `malloc()` en hot path de crypto/red | Use buffers estáticos |
| `memcmp()` para comparar claves/MAC | Timing side-channel |
| Hardcodear IPs o keys | Usar config o NVS |
| Committear auth keys | Son secrets |

## Port layer — referencia rápida

```c
/* RNG */
tsnode_err_t tsnode_port_random_bytes(uint8_t *out, size_t len);

/* Tiempo */
tsnode_err_t tsnode_port_uptime_ms(uint64_t *out_ms);
void tsnode_port_delay_ms(uint32_t ms);

/* Tasks */
tsnode_err_t tsnode_port_task_create(...);
void tsnode_port_task_delete_self(void);

/* Logging */
void tsnode_port_set_log(tsnode_port_log_fn fn);

/* TCP/TLS */
tsnode_err_t tsnode_port_tcp_connect(...);
tsnode_err_t tsnode_port_tls_connect(...);
tsnode_err_t tsnode_port_socket_write(...);
tsnode_err_t tsnode_port_socket_read(...);
void tsnode_port_socket_close(...);

/* UDP */
tsnode_err_t tsnode_port_udp_bind(...);
tsnode_err_t tsnode_port_udp_sendto(...);
tsnode_err_t tsnode_port_udp_recvfrom(...);
void tsnode_port_udp_close(...);

/* KV Storage */
bool tsnode_port_kv_get(...);
bool tsnode_port_kv_set(...);
void tsnode_port_kv_del(...);
```

## Estados del cliente (para parsing de logs)

```c
IDLE → FETCHING_KEY → HANDSHAKING → REGISTERING → MAP_SYNC → ONLINE
                                                          ↓
                                                     (polling loop)
```

## Referencia completa

Para reglas detalladas, ver `AGENTS.md`. Este documento es un resumen
ejecutivo para ganar tiempo en tareas comunes.
