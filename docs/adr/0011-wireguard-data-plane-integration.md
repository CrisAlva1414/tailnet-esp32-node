# ADR-0011: WireGuard data plane integration

- Estado: **propuesto**
- Fecha: 2026-08-30

## Contexto

El plano de control está completo y validado en hardware real: registro Noise/ts2021 sobre HTTP/2, identidad persistente en NVS, nodo aprobado con IP de tailnet asignada. El core WireGuard (`wg.c`, `replay.c`) está implementado y testeado en host (15/15 tests pasan). Falta cablear el data plane al cliente para que los paquetes fluyan realmente entre nodos de la tailnet.

El M5Stack Core 2 (banco de pruebas) tiene pantalla IPS 320x240 que se puede usar para un smoke test mostrando estado de conexión y IP asignada.

## Decisión

### Arquitectura de integración

1. **Tarea UDP dedicada** (`wg_recv_task`) separada del polling loop de MapRequest. Razón: el receive de paquetes WG es latencia-sensitive y no debe bloquearse por el polling HTTP que puede tardar segundos.

2. **Buffer estático circular** para paquetes WG entrantes (capacidad: 8 paquetes × 1500 bytes = 12KB). Evita heap allocation en el hot path de red.

3. **Interacción con MapResponse**: el polling loop actualiza peers en el WG device después de cada MapResponse exitoso. Los endpoints (IP:port) se extraen del netmap y se pasan a `tsnode_wg_peer_add()`.

4. **Encap outbound**: cuando una aplicación local envía un paquete IP destined a un peer, se resuelve vía `tsnode_wg_route()`, se encripta con `tsnode_wg_encap()`, y se envía por el UDP socket.

5. **Decap inbound**: la tarea UDP recibe, llama `tsnode_wg_decap()`, y si es handshake responde automáticamente; si es transport data, lo entrega al stack de red (TUN virtual o callback).

### Port layer: UDP socket

Extender `tsnode_port.h` con:
- `tsnode_port_udp_bind()` — bind a un puerto UDP
- `tsnode_port_udp_sendto()` — enviar datagrama a IP:port
- `tsnode_port_udp_recvfrom()` — recibir datagrama con timeout
- `tsnode_port_udp_close()` — cerrar socket

Implementación ESP-IDF sobre lwIP sockets (AF_INET, SOCK_DGRAM).

### Netmap parser: campos adicionales

Expandir `tsnode_map_peer_t` con:
- `endpoint_ip[16]` — IP del peer (extraída de Endpoints)
- `preshared_key[32]` — PSK si existe (all-zero si no)
- `allowed_ip` / `allowed_mask` — para cryptokey routing

### Smoke test con pantalla

Módulo `display.c` en `main/` que:
- Muestra SSID conectado
- Muestra IP de tailnet asignada (100.x.x.x)
- Muestra estado del cliente (conectando/sincronizando/online/error)
- Muestra peer count
- Se actualiza cuando cambia el estado del cliente

Usa textdraw directo de ESP-IDF (sin LVGL) para minimal footprint.

## Alternativas consideradas

- **Integrar receive WG al polling loop**: descartado porque el polling HTTP puede bloquear 2-5s y perderíamos paquetes WG durante ese window.
- **lwIP netif/TUN virtual**: más elegante pero agrega complejidad significativa. Para v1, el encap/decap directo sobre el socket UDP es suficiente — el ESP32 no necesita enrutar tráfico IP general, solo paquetes WG específicos entre peers de la tailnet.
- **LVGL para display**: overkill para un status display. Textdraw directo es ~200 líneas vs miles de LVGL.

## Consecuencias de seguridad

- Los paquetes WG recibidos se tratan como hostil por defecto (ADR-0008). El core WG ya valida MAC1/MAC2 y AEAD en cada paso.
- El buffer circular de paquetes tiene tamaño fijo en compile time — no hay heap allocation en el receive path.
- Los endpoints de peers vienen del control plane (Tailscale SaaS) y se confían como punto de partida, pero el core WG valida la identidad del peer en el handshake. Un atacante que falsifique un MapResponse no podría completar el handshake WG sin la private key del peer.
- La PSK de cada peer (si existe) se almacena en el peer_cfg y se usa solo en el handshake. All-zero significa "sin PSK" (válidamente seguro, solo autenticación por clave estática).

## Consecuencias de estabilidad

- Tarea UDP dedicada evita que el polling HTTP Cause drops de paquetes WG.
- Buffer circular con overflow policy (drop oldest) evita memory pressure bajo ráfaga.
- El smoke test de pantalla no impacta estabilidad — es solo display, sin lógica de negocio.
