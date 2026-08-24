# Runbook — reactivación del nodo ESP32 en la tailnet

Última actualización: 2026-08-24 (día de la desactivación voluntaria).

## Contexto de la desactivación

El nodo se validó end-to-end contra Tailscale SaaS (registro autorizado,
`MachineAuthorized:true`, IP <ip-tailnet> asignada) y luego se desactivó
eliminando el nodo del admin, porque el proyecto aún no tiene data plane
WireGuard: el dispositivo se registra pero no puede enviar ni recibir tráfico.
Menos nodos vivos = menos superficie. La reactivación es un procedimiento de
~10 minutos.

## Qué queda persistido en el dispositivo tras la desactivación

La NVS conserva tres cosas (sobreviven reinicios y desenchufes):

1. Credenciales Wi-Fi (SSID + PSK)
2. Auth key de Tailscale (si seguía vigente al apagar)
3. **Identidad del nodo** (machine key + node key, ADR-0003)

Consecuencia clave: mientras no se ejecute `tsforget`, el dispositivo siempre
se presentará con la MISMA identidad. Borrar el nodo en el admin NO borra esa
identidad; el próximo registro crea una entrada de nodo nueva con las mismas
claves y pide aprobación una única vez más.

## Prerequisitos

- **Auth key válida**: generar en https://login.tailscale.com/admin/settings/keys
  - Reutilizable, tag `tag:iot`, expiración corta (días).
  - Si la key cargada en el dispositivo sigue vigente, no hace falta otra.
  - Regla AGENTS.md §2.3: nunca commitear la key; cargarla por consola serie.
- ESP-IDF v5.5: `source ~/esp/esp-idf/export.sh`
- Dispositivo M5Stack Core 2 por USB; puerto serie típico `/dev/ttyACM0`.

## Pasos

1. **(Opcional) Rebuild + flash** si el firmware del dispositivo no está
   actualizado:
   ```
   idf.py -p /dev/ttyACM0 build flash
   ```
2. **Abrir consola serie** a 115200 baudios (`idf.py -p /dev/ttyACM0 monitor`
   o cualquier terminal serie).
3. **Verificar credenciales**: comando `provision status`.
   Debe reportar `wifi ssid: '...' | psk: cargada` y `auth key: cargada`.
   Si falta algo:
   - `wifi set <ssid>` → pide la PSK sin echo.
   - `tskey set` → pegar la auth key (sin echo), Enter.
4. **Conectar**: comando `tsconnect`. Flujo esperado en logs:
   `identity loaded from NVS` → Noise handshake OK → `RegisterResponse` →
   `machine not yet authorized (device approval)` → `registration done` →
   `MapResponse` / netmap.
   El mensaje "not yet authorized" NO es un error: significa que hay que
   aprobar (paso 5).
5. **Aprobar el nodo** en https://login.tailscale.com/admin/machines —
   aparece como `esp32-xxxxxx` pendiente de aprobación (device approval).
6. **Reconectar**: ejecutar `tsconnect` de nuevo. Esta vez el registro debe
   completarse SIN el warning de autorización.
7. **Verificar** desde otra máquina de la tailnet:
   ```
   tailscale status | grep esp32
   ```

## Reset de identidad o credenciales (opcional)

- `tsforget` — borra machine/node key de NVS. El próximo `tsconnect` genera
  identidad nueva → nodo nuevo → requiere aprobación. Solo para reset
  deliberado (ej. dispositivo cambia de dueño o de tailnet).
- `provision wipe` — borra Wi-Fi + auth key (reset completo de provisionamiento).

## Errores comunes

| Síntoma | Causa | Acción |
|---|---|---|
| `registration error: invalid key: API key ...` | auth key expirada/inválida | generar nueva y `tskey set` |
| `TCP connect failed` | Wi-Fi caída o DNS | verificar `status`, IP local |
| `machine not yet authorized` | falta device approval | aprobar en admin, re-ejecutar `tsconnect` |
| `error arrancando cliente: TSNODE_ERR_INVALID_STATE` | ciclo anterior aún corriendo | esperar unos segundos y reintentar |

## Limitaciones vigentes (ver README y docs/adr/0008)

- Sin data plane WireGuard: el nodo aparece "conectado" durante su ciclo
  register+map pero **no responde ping ni tráfico** de la tailnet.
- Ciclo one-shot: no hay polling ni reconexión automática todavía.
- Flash encryption / NVS encryption desactivadas: solo banco de pruebas
  (bloqueante antes de desplegar fuera de él, ver docs/adr/0003).
