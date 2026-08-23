# ADR-0008: Arquitectura de protocolo (ts2021 / Noise / WireGuard)

- Estado: aceptado
- Fecha: 2026-08-21

## Contexto

Con ADR-0001..0007 aceptados, el banco de pruebas (M5Stack Core 2, ESP32-D0WDQ6-V3)
ya provisiona Wi-Fi por consola serie y persiste credenciales en NVS. El siguiente
bloque de trabajo es el cliente Tailscale propiamente dicho. Este ADR fija la
arquitectura de protocolo: qué subconjunto de ts2021/Noise/WireGuard implementamos,
con qué primitivas criptográficas, y qué queda explícitamente fuera.

Per AGENTS.md §5.6, **todos los valores de protocolo citados abajo fueron
verificados contra la fuente primaria** (código de `tailscale/tailscale`, clone
shallow de `main` @ `0fd2f14`, agosto 2026) y no asumidos:

- `control/controlbase/handshake.go`: `protocolName = "Noise_IK_25519_ChaChaPoly_BLAKE2s"`;
  prologue = `"Tailscale Control Protocol v" + <uint16 versión>`; patrón IK con
  mensajes `-> e, es, s, ss` (cliente) y `<- e, ee, se` (servidor); HKDF-BLAKE2s;
  claves de sesión vía `Split()` (c1 = tx del cliente, c2 = rx del cliente).
- `control/controlbase/messages.go`: frame de iniciación = 101 bytes
  (2b versión BE | 1b tipo=0x01 | 2b largo payload BE (=96) | 32b efímera del
  cliente en claro | 48b machine key pública cifrada | 16b tag); frame de
  respuesta = 51 bytes (0x02 | 2b largo (=48) | 32b efímera del control | 16b tag);
  frames de error tipo 0x03 **no autenticados** (solo hints públicos).
- `control/controlbase/conn.go`: capa de registros = 1b tipo (0x04) + 2b largo BE;
  `maxMessageSize = 4096` por frame (cifrado útil máx. 4093); nonce ChaCha20 de
  12 bytes: primeros 4 bytes en cero, últimos 8 un contador big-endian que arranca
  en 1 (el 0 está reservado al handshake single-use), se incrementa por registro;
  agotamiento del contador (`^uint64(0)`) mata la conexión (reconnect obligatorio,
  no hay rekey).
- `control/controlhttp/`: transporte = HTTP GET a `http://<host>:80/ts2021` con
  `Upgrade: tailscale-control-protocol`; el servidor responde `101 Switching
  Protocols` y el TCP crudo pasa a llevar frames Noise. Fallback de compatibilidad:
  mismo diálogo dentro de TLS a `:443`. La clave pública del control plane se
  descubre por HTTPS normal en `/key?v=<CapabilityVersion>`.
- `tailcfg/tailcfg.go`: `CurrentCapabilityVersion = 145`. `RegisterRequest`
  (JSON sobre la conexión Noise, POST `/machine/register`) incluye `Version`,
  `NodeKey`, `Auth.RegisterResponseAuth.AuthKey`, `Hostinfo`, `Expiry`,
  `Ephemeral`. `RegisterResponse` incluye `MachineAuthorized`, `AuthURL`
  (si falta autorización interactiva), `Error`.
- `control/tsp/{register,map}.go` y `control/controlclient/direct.go`: registro en
  `/machine/register`, netmap por streaming long-poll en `/machine/map`
  (`MapRequest{Version, KeepAlive, NodeKey, DiscoKey, Stream, Hostinfo}` →
  secuencia de `MapResponse`).
- `disco/disco.go`: descubrimiento de pares (Ping/CallMeMaybe sobre UDP) + relay
  DERP como fallback cuando no hay ruta directa.
- Toolchain local (ESP-IDF v5.5): mbedTLS 3.6.3 provee X25519
  (`MBEDTLS_ECP_DP_CURVE25519`) y ChaCha20-Poly1305 (`mbedtls/chachapoly.h`);
  **no incluye BLAKE2s** (verificado: no existe ningún archivo blake2* en el árbol).

El proyecto de referencia (`alfs/tailscale-iot`) demuestra que este subconjunto es
viable en ESP32 pero sin DERP, con resets por timing crypto y con `noise-c`
vendorizado y parcheado — exactamente los atajos que AGENTS.md §6 nos prohíbe
repetir sin ADR.

## Decisión

### D1. Alcance v1 del protocolo

Implementamos, en orden de dependencia:

1. **Transporte ts2021**: dial HTTP:80 a `/ts2021` con upgrade, handshake Noise IK
   completo, capa de registros con límites duros de frame (4096) y contador de
   nonce verificado. El fallback TLS:443 **queda fuera de v1** (ver D5).
2. **Registro**: POST JSON de `RegisterRequest` con auth key one-off ya
   provisionada (ADR-0007), node key generada localmente; tratamiento de
   `RegisterResponse` incluyendo el caso `AuthURL` no vacío → error explícito
   "se requiere autorización interactiva" (no intentamos automatizarla).
3. **Netmap**: cliente `/machine/map` en modo **polling no-streaming**
   (`Stream=false`: una MapResponse por conexión) con intervalo configurable y
   backoff exponencial con jitter. El streaming incremental queda para una
   iteración posterior si el polling demostrara costos inaceptables.
4. **Data plane WireGuard**: túnel UDP directo usando las claves de nodo y los
   endpoints/AllowedIPs del netmap. Solo rutas directas; **sin DERP** (limitación
   heredada y documentada, corolario de seguridad de AGENTS.md §2.2: si no hay
   ruta directa, el nodo reporta estado "sin ruta" y no inventa fallbacks).

Fuera de alcance v1 (sin ADR posterior no se implementan): DERP, disco completo,
IPv6, subnet routing, exit node, MagicDNS local, tailnet lock, rekey automático
(reconexión completa en su lugar).

### D2. Módulos internos de tsnode (capa core, C puro, sin headers de plataforma)

```
components/tsnode/src/
├── tsnode.c              # máquina de estados de alto nivel (existente)
├── proto/
│   ├── ts2021.c/.h       # dial+upgrade HTTP, framing de registros, límites
│   ├── noise_client.c/.h # handshake IK (estado simétrico, MixDH, Split)
│   ├── register.c/.h     # RegisterRequest/RegisterResponse (JSON mínimo propio)
│   └── map.c/.h          # MapRequest/MapResponse (parseo defensivo, caps fijos)
├── crypto/
│   └── blake2s.c/.h      # BLAKE2s clean-room desde RFC 7693 (ver D4)
└── wg/
    ├── wg.c/.h           # formato de mensaje WG, handshake cookie/MAC, session
    └── replay.c/.h       # ventana anti-replay (ya prevista por AGENTS.md §2.2)
```

La capa port (ADR-0006) gana dos operaciones: socket UDP/TCP con timeout y
TLS-cliente para el fetch de `/key` (única operación TLS de v1).

### D3. Máquina de estados del nodo

`DISCONNECTED → CONTROL_KEY_FETCH → NOISE_HANDSHAKE → REGISTERING → MAP_SYNC →
WG_ACTIVE`, con transiciones de error hacia `BACKOFF` (backoff exponencial con
jitter, techo configurable). Cada transición emite evento observable por la app
(consola). El estado vive en un contexto único, single-threaded (una tarea
FreeRTOS dueña del ciclo), sin compartición de estado entre tareas en v1.

### D4. Primitivas criptográficas

- **X25519 y ChaCha20-Poly1305**: mbedTLS (ya presente en ESP-IDF, auditada,
  mantenida por Espressif/Muped upstream). Cumple la preferencia de AGENTS.md §6.
- **BLAKE2s**: mbedTLS no lo trae. Se implementa clean-room desde RFC 7693 en
  `tsnode/crypto/blake2s.c` (~200 líneas, sin dependencias), con los test vectors
  oficiales del RFC y del paquete de referencia BLAKE2 en
  `tests/unit/protocol_vectors/`. Alternativas descartadas: vendorizar `noise-c`
  (patrón PoC prohibido salvo ADR, arrastra más superficie que una sola función
  hash), monocypher (trae BLAKE2b, no BLAKE2s; integrarlo solo por esto no paga).
- **HKDF-BLAKE2s**: trivial sobre nuestra BLAKE2s (HMAC opcional del RFC; Noise
  usa BLAKE2s en modo keyed/MAC donde corresponde según spec Noise).
- Comparación de tags/claves públicas siempre en tiempo constante (helper propio
  sobre mbedTLS `mbedtls_ct_memcmp`), per AGENTS.md §4.

### D5. Decisiones de transporte acotadas

- **Sin fallback TLS:443 en v1**: el path feliz (HTTP:80 + upgrade) es el único
  soportado; si falla, backoff y reintento. Razón: el fallback exige un stack TLS
  cliente con verificación de certificados en el ESP32 (RAM/CPU no triviales) para
  un camino que en despliegues reales es la excepción. Riesgo aceptado y documentado:
  redes que interceptan puerto 80 dejarán al nodo sin control plane; el nodo lo
  reporta y no degrada su seguridad para conectarse igual. Un ADR futuro puede
  activar el fallback si el entorno de despliegue lo exige.
- **Fetch de `/key?v=145`**: única excepción TLS (HTTPS estándar con CA store de
  ESP-IDF). La clave pública obtenida se persiste y se fija (pinning): handshakes
  posteriores contra una clave distinta fallan ruidosamente hasta reprovisionar.
  Esto convierte el primer fetch en el único punto de confianza del diseño.

### D6. Presupuesto de memoria (objetivo medible, no aspiracional)

Buffers estáticos para todo el hot path: 1 buffer de frame Noise (4 KiB), 1 buffer
de ensamblado de MapResponse (cap inicial 16 KiB, ajustable por medición real),
contextos mbedTLS X25519/ChaCha20 (estáticos vía `mbedtls_*_init` + contexto
dedicado). Objetivo: heap dinámico solo durante setup de mbedTLS del fetch `/key`,
cero mallocs en el ciclo estable handshake→map→wg. La medición real (heap high-water
mark antes/después de cada fase) se registra en la entrada de sesión que integre
este código; si el presupuesto no cierra, se vuelve a este ADR antes de recortar
verificaciones.

## Alternativas consideradas

- **Portar el cliente Go completo** (tailscaled): descartado — Go no corre en
  ESP32-C3 con presupuesto de RAM razonable y arrastraría toda la superficie que
  no necesitamos.
- **Vendorizar `noise-c`** como hizo el proyecto de referencia: descartado por
  AGENTS.md §6 — proyecto sin mantenimiento activo, requiere parches build-time,
  y solo necesitaríamos de él BLAKE2s + orquestación Noise que de todos modos
  debemos escribir (noise-c no implementa el framing ni el prologue de Tailscale).
- **Implementar Noise sobre monocypher**: viable, pero duplicaría X25519/ChaCha20
  que mbedTLS ya provee auditada y mantenida en nuestro toolchain; dos libs crypto
  en vez de una agranda la superficie de auditoría sin beneficio.
- **Streaming `/machine/map` desde v1**: más eficiente en radio, pero complica el
  parser (chunks parciales, reconexión de sesión con `MapSessionHandle`). El
  polling simple es auditable primero; optimizar después con datos reales.
- **Disco mínimo (Ping/CallMeMaybe) en v1**: necesario solo para NAT traversal
  entre redes distintas. En el banco de pruebas (LAN directa) no aporta; su
  complejidad (endpoint discovery, rate limiting, spoofing de CallMeMaybe) merece
  ADR propio. Queda como candidato natural para ADR-0009.

## Consecuencias de seguridad

- **Superficie nueva expuesta a red hostil**: parser HTTP del upgrade, framing
  Noise, parser JSON de RegisterResponse/MapResponse, mensajes WireGuard entrantes.
  Todo ello se construye bajo las reglas §2.2/§4 (validación de longitud antes de
  tocar buffers, caps en compile-time, sin libc sin límite) y con fuzzing planificado
  desde ahora: los parsers `map.c` y `wg/replay.c` deben tener corpus de fuzz
  definido en sus tests unitarios iniciales (host-side), no después.
- **Pin de la clave del control plane** (D5): elimina la clase de ataque "MITM con
  clave de control falsa" tras el primer fetch; el primer fetch queda protegido por
  TLS estándar. Queda anotado como riesgo residual que el primer aprovisionamiento
  confía en la CA store del dispositivo.
- **Frames de error Noise no autenticados** (tipo 0x03): se tratan como hints
  públicos — nunca se loguean textualmente sin sanitización y nunca alteran estado
  más allá de "backoff y reintento".
- **Sin DERP** (D1): el nodo solo alcanza peers con ruta directa. No se mitiga con
  canales alternativos inseguros; se reporta. Limitación conocida, coherente con
  §2.2.
- **BLAKE2s propia** (D4): introduce código crypto nuevo no auditado externamente.
  Mitigación: implementación mínima conforme RFC 7693, test vectors oficiales
  obligatorios en CI, y revisión cruzada línea a línea contra el RFC antes de
  mergear. El riesgo se acota a una función hash pura sin estado secreto.
- Sin cambios en el modelo de amenaza físico (§2.1): las claves de nodo/machine
  siguen almacenándose según ADR-0003.

## Consecuencias de estabilidad

- **Presupuesto de tiempo crypto**: el handshake Noise implica 3 X25519 + varios
  ChaCha20-Poly1305; el ciclo de WG añade los suyos. Per lección del proyecto de
  referencia (§2.2), cada fase se instrumenta con medición de duración y feed del
  task watchdog desde el día uno; los umbrales medidos se documentan en la sesión
  de integración. Prohibido "subir el watchdog hasta que no resetee" sin medición
  registrada.
- **Backoff con jitter** en toda reconexión evita tormentas de reintento contra el
  control plane y contra el radio Wi-Fi (coordinado con el rate-limit existente de
  wifi_app).
- **Polling vs streaming**: el polling genera más conexiones (handshake Noise por
  poll). Si la medición muestra degradación (batería/radio/CPU), la migración a
  streaming será un cambio acotado a `proto/map.c` gracias al aislamiento de capas.
- Buffers estáticos dimensionados en compile-time eliminan la fragmentación de heap
  como causa de crash de larga duración en el hot path (riesgo §4 de AGENTS.md).
