# 2026-08-24 — HTTP/2 sobre Noise: implementación completa y verificada

## Contexto

Resolver el bloqueo de la sesión anterior: el nodo completaba handshake Noise
pero nunca aparecía en https://login.tailscale.com/admin/machines. Causa raíz
confirmada: tras el upgrade ts2021, el control plane habla **HTTP/2 dentro del
túnel Noise**; sin preface h2 respondía "bogus greeting" y cerraba. Esta sesión
implementó el cliente HTTP/2 mínimo definido en ADR-0009 (decisiones D1–D4),
junto con las correcciones de framing que la sesión "blocked-plan-next" dejó
planificadas.

## Cambios

- **ADR-0009** (`docs/adr/0009-minimal-http2-over-noise.md`): decisiones D1
  (h2 mínimo propio, sin nghttp2), D3 (buffers estáticos, cero heap), D4
  (I/O inyectable para testeo host).
- **`components/tsnode/src/proto/h2.{c,h}`** (nuevo): cliente HTTP/2 mínimo.
  - Preface + SETTINGS inicial (ENABLE_PUSH=0); streams secuenciales impares.
  - HPACK salida: literal-sin-indexing, sin Huffman, orden fijo `:method`,
    `:scheme`, `:authority`, `:path`, `[ts-lb]`, `content-type`; índices
    estáticos 3/7/1/4/31. Entrada: solo acepta HEADERS cuyo primer byte sea
    0x88 (`:status 200`) — todo lo demás fail-closed.
  - Frames: SETTINGS/PING (PONG automático)/WINDOW_UPDATE/PRIORITY manejados;
    GOAWAY/RST_STREAM/PUSH_PROMISE/CONTINUATION/PADDED/tipos desconocidos →
    error. DATA acumulada en buffer estático con overflow check.
  - TX chunked a `H2_MAX_RECORD_PLAINTEXT` (4077 B); RX acumulador con techo
    compile-time (frame máx + overhead).
- **`tsnode_register.c`**: hostname movido a `"Hostinfo":{"OS":"linux",...}`
  anidado (validado contra producción con probe Python).
- **`tsnode_map.{c,h}`**: nueva `tsnode_map_parse_framed()` (framing tsp
  `[u32 LE][JSON]`, validación exacta declared==len, detección zstd → error
  explícito). Archivo limpiado a C puro (sin includes de plataforma ni logging
  interno) — cumple guard ADR-0006.
- **`tsnode_client.c`**: Steps 7–9 rewirados vía `h2_client_start()` +
  `h2_post()`; eliminados los hacks de prebuffer/frames pequeños. Respuestas
  h2 en buffers estáticos (no stack). Semántica: `auth_url` ≠ "" →
  TSNODE_ERR_PROVISIONING; `MachineAuthorized:false` → warning + continúa
  (device approval es flujo normal).
- **Guard ADR-0006 arreglado** (CI venía rojo desde hacía 5 commits):
  - `x25519_wrapper.c` movido a `src/port/esp_idf/` (usa `esp_random.h`);
    header queda en `src/port/`.
  - `tsnode_client.c`: `vTaskDelete(NULL)` reemplazado por nueva abstracción
    `tsnode_port_task_delete_self()` (port header + impl ESP-IDF); quitados
    los includes de FreeRTOS del core.
- **Tests host**: `tests/unit/test_h2.c` + `Makefile`. 15 tests sobre mock
  I/O en memoria, incluyendo vectores HPACK capturados de producción
  (SETTINGS real de 36 B, headers register/map exactos) y casos adversarios
  (frames oversize, status ≠ 200, GOAWAY, EOF mid-stream, zstd, length
  mentiroso). **15/15 PASS.**
- `.gitignore`: binario `tests/unit/test_h2`.

## Decisiones de seguridad tomadas o revisadas

- Todo input de red h2 tratado como hostil: longitudes validadas antes de
  tocar buffers, techos compile-time, frames no reconocidos rechazan la
  conexión (fail-closed) en vez de ignorarse silenciosamente (ADR-0009).
- Sin heap en hot path h2 (D3): elimina vector de fragmentación/corrupción
  bajo uso sostenido.
- Verificación ejecutada esta sesión: guard ADR-0006 OK, cppcheck 2.17.1 con
  flags versionados → 0 hallazgos, `-Werror` confirmado activo en build,
  `idf.py build` OK (ESP32, bin 0xe7ad0 B, partición app 38% libre).
- La auth key que figuraba en `docs/sessions/2026-08-24-blocked-plan-next.md`
  **nunca entró al historial git** (el archivo era untracked). El archivo se
  borró según su propia nota. Rotación de la key igualmente recomendada por
  higiene.

## Pendiente / bloqueado

- Probar en hardware real (M5Stack Core2, `/dev/ttyACM0`) y verificar que el
  nodo aparece en el admin de Tailscale; aprobar el dispositivo si hace falta
  (device approval).
- Flow control h2 / body > 16384 B: hoy `h2_post` rechaza bodies mayores con
  INVALID_ARG. Suficiente para register/map actuales; revisar si MapRequest
  crece (anotado en ADR-0009 como limitación conocida).
- Polling loop de map y lógica de reconexión (TODO existente en client).
