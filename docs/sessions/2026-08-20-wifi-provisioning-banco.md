# 2026-08-20 — Wi-Fi real en el banco: provisioning por consola y conexión

## Contexto

El usuario aceptó explícitamente los ADRs 0002–0007 en el chat y aportó
credenciales del entorno de prueba (SSID/PSK por chat; auth key Tailscale
también por chat — ver decisiones de seguridad). Objetivo: primer hito real
del dispositivo en red.

## Cambios

- ADRs 0002–0007 pasados a `aceptado` (confirmación única del usuario).
- AGENTS.md §4 actualizado: `-std=gnu11 -Wall -Wextra -Werror` en firmware,
  pedantic completo reservado a tests host (desviación ya documentada en
  `docs/format/c-style.md`, ahora formalizada con confirmación).
- `sdkconfig.defaults` — creado: flash 16MB (Core 2), partición factory
  agrandada, logs INFO, stack de main 4096. Sin flags de prod a propósito
  (ADR-0003).
- Port real en `components/tsnode`: `tsnode_port_random_bytes` vía
  `esp_fill_random`, `tsnode_port_uptime_ms` vía `esp_timer`. REQUIRES
  mínimos declarados.
- App de referencia (`main/`): `prov_store.c` (NVS namespace
  `tsnode_prov`), `wifi_app.c` (STA + reconexión con rate-limit),
  `console.c` (consola serie ADR-0007: secretos sin echo, validación
  sintáctica de auth key, wipe), `main.c` reescrito.

## Decisiones de seguridad tomadas o revisadas

- Credenciales Wi-Fi provisionadas SOLO por consola serie sin echo;
  jamás hardcodeadas ni logueadas. Verificado en vivo.
- **Auth key expuesta en chat pendiente de revocación por el usuario** —
  recordado al cierre; no se usó ni se almacenó esa key en ningún lado.
- PSK en NVS queda en claro en este build dev: válido SOLO en banco
  (ADR-0003/0007). No salir de acá con esta imagen.
- Bug corregido en el camino: doble `esp_wifi_connect` (STA_START con
  config vacía) — ahora gated por flag `s_config_applied`.

## Pendiente / bloqueado

- Usuario debe revocar la auth key expuesta en chat y generar una fresca
  cuando exista el stack de registro.
- Próximo hito: ADR de arquitectura de protocolo (ts2021 sobre Noise) →
  recién ahí la auth key tiene dónde consumirse.
- Cosmético detectado: bytes basura intercalados en el echo de consola
  (quirk del CH9102F en esta captura); el filtro de input los hace
  inofensivos — no afectan parsing.

## Resultado del banco

Boot con credenciales persistidas → asociación WPA2 automática → DHCP:
**IP LAN asignada** (valor en doc privada, ADR-0010), rssi -44, canal 6.
`status` por consola confirma
estado. Flash 16MB sin warnings.
