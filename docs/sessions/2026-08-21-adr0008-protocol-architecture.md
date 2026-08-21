# 2026-08-21 — investigación y ADR-0008 de arquitectura de protocolo

## Contexto

Con el banco de pruebas operativo (Wi-Fi + provisioning por consola, sesión
2026-08-20), el siguiente bloque es el cliente Tailscale en sí. AGENTS.md §5.6
exige verificar todo valor de protocolo contra fuente primaria antes de escribir
código, así que la sesión se dedicó a investigar ts2021/Noise/WireGuard desde el
código de `tailscale/tailscale` y formalizar el resultado como ADR-0008.

## Cambios

- Clone shallow de `tailscale/tailscale` (main @ `0fd2f14`) en `/tmp/opencode/`
  para consulta local.
- Verificación primaria de: nombre del protocolo Noise
  (`Noise_IK_25519_ChaChaPoly_BLAKE2s`, `control/controlbase/handshake.go`),
  framing exacto de handshake (101 B iniciación / 51 B respuesta,
  `control/controlbase/messages.go`), capa de registros (frames ≤ 4096 B, nonce
  contador BE desde 1, sin rekey — `conn.go`), transporte HTTP upgrade
  (`/ts2021`, `Upgrade: tailscale-control-protocol`, fallback TLS:443),
  descubrimiento de clave del control plane (`/key?v=145`,
  `CurrentCapabilityVersion = 145`), flujo de registro (`RegisterRequest` con
  `Auth.AuthKey` → `RegisterResponse` con `AuthURL`/`MachineAuthorized`) y netmap
  (`/machine/map`, streaming o single-shot).
- Verificación del toolchain local: mbedTLS 3.6.3 (ESP-IDF v5.5) provee X25519 y
  ChaCha20-Poly1305; **no incluye BLAKE2s**.
- Creado `docs/adr/0008-protocol-architecture-ts2021-noise-wireguard.md`
  (estado: propuesto). Commit `2a0b3c2`, CI verde (run 32445819724).

## Decisiones de seguridad tomadas o revisadas

- Pin de la clave pública del control plane tras el primer fetch HTTPS de `/key`
  (única operación TLS de v1); handshakes contra otra clave fallan ruidosamente.
- Fallback TLS:443 de ts2021 excluido de v1: si puerto 80 está interceptado, el
  nodo reporta y reintenta; no se degrada seguridad para conectar.
- Frames de error Noise (tipo 0x03) no autenticados: solo hints, nunca logueados
  crudos ni alteran estado más allá de backoff.
- BLAKE2s clean-room desde RFC 7693 con test vectors oficiales obligatorios en CI
  (mbedTLS no lo trae; noise-c descartado por §6).
- Fuzzing de parsers (`map.c`, `wg/replay.c`) exigido en los tests iniciales,
  no pospuesto.

## Pendiente / bloqueado

- **ADR-0008 espera aceptación del usuario** antes de escribir código de protocolo
  (bloqueante per AGENTS.md §5.1).
- Recordatorio pendiente de confirmación: revocar la auth key que apareció en el
  chat el 2026-08-20 (no fue usada ni almacenada).
- Siguiente sesión post-aceptación: esqueleto de `proto/ts2021.c` +
  `crypto/blake2s.c` con test vectors, y medición inicial de timing crypto.
