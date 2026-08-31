# 2026-08-24 — Plan de integración WireGuard data plane

## Contexto

Con el plano de control completo y validado en hardware (registro Noise/ts2021 sobre HTTP/2, identidad persistente en NVS, nodo aprobado con IP de tailnet asignada), el siguiente paso es integrar el data plane WireGuard para completar la conectividad end-to-end.

## Estado actual

- **Control plane**: completado y validado
  - Noise handshake IK sobre HTTP/2 (ADR-0008)
  - RegisterRequest/RegisterResponse con auth key one-off
  - MapRequest/MapResponse parsing
  - Identidad persistente en NVS (ADR-0003)
- **WireGuard core**: completado y testeado
  - Handshake IKpsk2 (create/consume initiation + response)
  - Transport ChaCha20-Poly1305 con anti-replay
  - Cryptokey routing IPv4 longest-prefix
  - 70+ tests pasando (vectores RFC 7748/8439)

## Pendiente de integración

### 1. Polling loop de MapRequest/Response

**Archivo**: `components/tsnode/src/proto/tsnode_client.c`

El cliente actual es one-shot: completa el registro y termina. Necesita:
- Loop que envíe MapRequest periódicamente para mantener la sesión
- Backoff exponencial con jitter en caso de error
- Parseo de MapResponse actualizado para detectar cambios de peers
- Manejo de reconexión si la conexión se pierde

**Implementación**:
```c
/* Después del registro exitoso en do_connect() */
while (s_state == TSNODE_CLIENT_ONLINE) {
    // 1. Enviar MapRequest (poll)
    // 2. Parsear MapResponse
    // 3. Actualizar peers de WireGuard si hay cambios
    // 4. Esperar intervalo (con backoff en caso de error)
    // 5. Verificar watchdog
}
```

### 2. Integración WireGuard data plane

**Archivos**: 
- `components/tsnode/src/proto/tsnode_client.c` (orquestación)
- `components/tsnode/src/wg/wg.c` (core WG existente)

Flujo:
1. Al recibir MapResponse con peers:
   - Extraer `public_key`, `endpoints`, `allowed_ips` de cada peer
   - Configurar `tsnode_wg_peer_add()` con esta información
   - Iniciar handshake WG con cada peer (create_initiation)
2. Para cada peer con sesión establecida:
   - Enviar keepalives WG periódicamente
   - Encriptar/decryptar tráfico de应用
3. Manejar reconexión:
   - Si el mapeo falla, reconectar al control plane
   - Re-registrar y re-obtener el mapa

### 3. UDP socket para tráfico WireGuard

**Archivo**: `components/tsnode/src/port/` (nueva función en el port layer)

WireGuard opera sobre UDP directo. Necesitamos:
- Socket UDP que pueda enviar/recibir paquetes WG
- Binding a un puerto local (típicamente 51820)
- Manejo de NAT traversal (para v1, solo UDP directo)

**API del port**:
```c
tsnode_err_t tsnode_port_udp_open(tsnode_port_socket_t **sock, uint16_t port);
tsnode_err_t tsnode_port_udp_send(tsnode_port_socket_t *sock, 
                                   const struct sockaddr_in *addr,
                                   const uint8_t *data, size_t len);
tsnode_err_t tsnode_port_udp_recv(tsnode_port_socket_t *sock,
                                   struct sockaddr_in *addr,
                                   uint8_t *buf, size_t cap, size_t *len,
                                   uint32_t timeout_ms);
```

### 4. Flujo completo de integración

```
Control Plane                    Data Plane
     |                              |
     |--- MapRequest -------------->|
     |<-- MapResponse (peers) ------|
     |                              |
     |--- Configurar WG peers ----->|
     |--- Iniciar handshake WG ---->|
     |                              |
     |<-- WG Handshake Response ----|
     |--- Establecer sesión WG ---->|
     |                              |
     |<-- UDP tráfico WG ---------->|
     |--- UDP tráfico WG ---------->|
```

## Decisiones de seguridad pendientes

1. **Rate limiting de MapRequest**: ¿cada cuánto hacer poll? (ADR-0008 D3 dice "intervalo configurable con backoff")
2. **Manejo de peers obsoletos**: ¿qué hacer cuando un peer desaparece del MapResponse?
3. **Rekey automático**: ¿implementar o dejar para v2? (ADR-0008 D1 dice "rekey automático fuera de v1")

## Próximos pasos inmediatos

1. Implementar polling loop en `tsnode_client.c`
2. Agregar parsing de peers del MapResponse para WG
3. Integrar llamadas a `tsnode_wg_*` en el cliente
4. Agregar UDP socket al port layer
5. Crear prueba de integración completa
6. Probar en hardware real

## Riesgos conocidos

- **Memoria**: el cliente ya usa buffers grandes (32KB para MapResponse). Agregar WireGuard puede requerir ajustar el presupuesto de memoria (ADR-0008 D6).
- **CPU**: el crypto WG en ESP32-C3 puede ser costoso. Medir tiempo de handshake y transport bajo carga real.
- **NAT traversal**: sin soporte completo de disco (ADR-0008 D1), solo funciona en red directa o con port forwarding explícito.