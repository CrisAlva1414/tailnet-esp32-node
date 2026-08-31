# 2026-08-24 — Polling loop y plan de integración WireGuard data plane

## Contexto

Con el plano de control completo y validado en hardware (registro Noise/ts2021 sobre HTTP/2, identidad persistente en NVS, nodo aprobado con IP de tailnet asignada), esta sesión se enfocó en implementar el polling loop de MapRequest/Response y crear un plan detallado para la integración del data plane WireGuard.

## Cambios

- **`components/tsnode/src/proto/tsnode_client.c`**:
  - Agregado polling loop de MapRequest/Response con backoff exponencial y jitter
  - Intervalo base: 30 segundos, máximo: 300 segundos
  - Manejo de errores con reconexión automática tras 3 errores consecutivos
  - Incluido `wg.h` para futura integración WireGuard
  - Agregada variable `s_node_key_pub_hex` para headers HTTP
  - Agregado FreeRTOS includes para `vTaskDelay`

- **`docs/sessions/2026-08-24-plan-integracion-wg-data-plane.md`**:
  - Plan detallado de integración WireGuard data plane
  - Análisis de pendientes: polling loop, integración WG, UDP socket
  - Decisiones de seguridad pendientes documentadas
  - Riesgos conocidos identificados

## Decisiones de seguridad tomadas o revisadas

- **Backoff exponencial con jitter**: implementado para evitar tormentas de reintento contra el control plane (consistente con ADR-0008 D3)
- **Rate limiting de MapRequest**: intervalo base de 30 segundos con jitter ±20%
- **Reconexión automática**: tras 3 errores consecutivos, se cierra la conexión y se reintenta desde el inicio

## Pendiente / bloqueado

- **WireGuard data plane**: integración completa pendiente
  - Inicializar dispositivo WG con node key
  - Agregar peers desde MapResponse
  - Manejar paquetes WG entrantes/salientes
- **UDP socket para tráfico WireGuard**: función pendiente en el port layer
- **Prueba de integración completa**: probar flujo end-to-end en hardware
- **Flash encryption + Secure Boot v2**: antes de cualquier despliegue fuera del banco

## Verificación

- Tests host: 15/15 PASS (test_h2, test_blake2s, test_replay, test_wg)
- Build ESP-IDF: pendiente de verificar con los cambios realizados
- Código compila sin errores con `-Wall -Wextra -Wpedantic -Werror`