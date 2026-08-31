# 2026-08-30 — WireGuard data plane integration + smoke test display

## Contexto

Completar la integración del data plane WireGuard en firmware (cablear el WG core ya implementado y testeado en el host al cliente en tsnode_client.c), y agregar un smoke test con display para el M5Stack Core2 que muestre el estado de conexión.

## Cambios

### ADR-0011: WireGuard data plane integration (`docs/adr/0011-wireguard-data-plane-integration.md`)
- Define la arquitectura: tarea UDP dedicada (`wg_recv_task`) separada del polling loop
- Buffer circular estático para paquetes WG entrantes (sin heap)
- Port layer: UDP socket API (`udp_bind`, `udp_sendto`, `udp_recvfrom`, `udp_close`)

### Netmap parser expandido (`components/tsnode/src/proto/tsnode_map.h`, `tsnode_map.c`)
- `tsnode_map_peer_t` ahora incluye: `endpoint_ip`, `endpoint_port`, `allowed_ip`, `allowed_mask`, `preshared_key`
- Parser extrae IP del endpoint (no solo port), CIDR a mask conversion, PSK hex

### Port layer: UDP socket (`components/tsnode/src/port/tsnode_port.h`, `tsnode_port_net_esp_idf.c`)
- Nuevas funciones: `tsnode_port_udp_bind`, `tsnode_port_udp_sendto`, `tsnode_port_udp_recvfrom`, `tsnode_port_udp_close`
- Implementación sobre lwIP sockets (AF_INET, SOCK_DGRAM) con `select()` para timeout
- Socket non-blocking para recv con timeout

### WireGuard integration en cliente (`components/tsnode/src/proto/tsnode_client.c`)
- `init_wg_device()`: inicializa WG device con node key y crypto mbedTLS
- `init_wg_socket()`: bind UDP port 51820
- `find_peer_by_key()`: busca peer existente por WG public key
- `update_wg_peers()`: sincroniza netmap → WG peers, inicia handshake si no hay sesión activa
- `wg_recv_task()`: tarea dedicada que recibe paquetes WG, procesa initiation/response/transport, envía respuestas automáticamente
- Integrado en secuencia de conexión: WG init → socket bind → recv task create → polling loop

### Smoke test display (`main/display.h`, `main/display.c`)
- Init ILI9342 (ST7789-compatible) vía esp_lcd sobre SPI2
- Font 5x7 ASCII bitmap embebida para texto renderizado en frame buffer
- Funciones: `display_splash()`, `display_wifi()`, `display_tailscale()`, `display_status()`
- Colores: verde=ok, rojo=error, amarillo=waiting, cyan=labels

### Main app (`main/main.c`)
- Display init al boot (splash screen)
- Tarea `status_display_task` en core 1: actualiza WiFi IP, estado TS, peer count cada 3s

### Build fixes
- `replay.c` agregado a SRCS del componente tsnode (faltaba)
- `lwip` agregado a REQUIRES del componente tsnode (para UDP sockets)
- `tsnode` agregado a REQUIRES de main (para `tsnode_client_state_get`)
- Forward declarations para funciones WG en tsnode_client.c
- `TSNODE_LOGD` → `TSNODE_LOGI` (LOGD no existe en port layer)
- `sys/select.h` en vez de `poll.h` (ESP-IDF compatible)

## Decisiones de seguridad tomadas o revisadas

- UDP socket en puerto 51820 (estándar WireGuard) — no exponer más puertos de los necesarios
- Peer validation: solo peers con endpoint IP válida son agregados al WG device
- Handshake initiation se envía automáticamente al detectar peer sin sesión activa
- Replay protection ya implementada en WG core (testada en tests previos)

## Pendiente / bloqueado

- **Prueba end-to-end en hardware real**: flashear M5Stack Core2 y verificar que el WG handshake se completa con otro nodo de la tailnet
- **Flash encryption + Secure Boot v2**: antes de despliegue fuera del banco de pruebas
- **ADR-0009**: actualizar estado a "aceptado" (código ya implementado y funcionando)
- **Fuzzing de parsers**: exigido por ADR-0008 pero no implementado aún
- **Medición de heap high-water mark**: ADR-0008 D6 la exige

## Verificación

- Build ESP-IDF: **PASS** (target esp32, clean build)
- Tests host: **15/15 h2 PASS**, blake2s ALL PASS, replay ALL PASS, wg ALL PASS
- Tamaño del binario: verificable con `idf.py size`
