# 2026-08-24 — validación en hardware contra producción e identidad persistente

## Contexto

Con la pila HTTP/2-sobre-Noise ya commiteada (`8a6340f`) y CI en verde, esta sesión
buscó cerrar el ciclo end-to-end en hardware real: registrar el ESP32-C3/M5Stack Core2
en el tailnet real del usuario (Tailscale SaaS), superar la aprobación de dispositivo,
y dejar la identidad estable entre reinicios.

## Cambios

- **Causa raíz del HTTP 500 en producción encontrada y corregida**: los builders de
  register/map recibían punteros a buffers de **stack de la consola** como `auth_key`
  y `hostname`. El cliente copiaba el struct pero no los strings; al retornar
  `cmd_tsconnect_common()` esa memoria se reciclaba y el JSON salía con basura
  (visible en el server Go local: `"Hostname":"\xef\xbf\xbd..."`, AuthKey corrupta).
  El stub local respondía 200 igual (no valida); producción valida → 500.
  Fix: `tsnode_client_start()` ahora copia `control_host`, `auth_key` y `hostname`
  a storage propio estático con límites definidos (`TSNODE_CLIENT_*_MAX`).
- **Fix NodeKey**: los builders recibían la clave privada donde iba la pública.
  Call sites usan `s_node_key_pub`; parámetros renombrados a `node_key_pub`
  en `tsnode_register.{c,h}` (se eliminó el parámetro `machine_key` muerto)
  y `tsnode_map.{c,h}`.
- **Fix pushback h2**: la rama no-EarlyNoise descartaba registros post-handshake;
  con un server sin EarlyNoise (server Go local, upgrade HTTP) el SETTINGS inicial
  se perdía. Ahora hay buffer pushback estático servido por `h2_io_recv` antes del
  transporte; el overshoot del drain de EarlyNoise también va a pushback.
- **Identidad persistente (ADR-0003)**: machine key + node key se cargan de NVS
  (namespace `tsnode`, blobs `machkey`/`nodekey`) y si no existen se generan y
  persisten. La aprobación de device sobrevive reinicios y ya no se crean nodos
  duplicados por intento. Comando consola `tsforget` para borrar identidad.
- **Estado DONE**: el cliente one-shot dejaba estado ONLINE zombie tras
  auto-borrarse, bloqueando reinicios (`TSNODE_ERR_INVALID_STATE`). Nuevo estado
  terminal `TSNODE_CLIENT_DONE`, aceptado por `tsnode_client_start()`.
- Modo test-only `tsconnectlocal` + campo `config.control_key_hex` para probar
  contra el stub Go local sin fetch de `/key`.
- Logging temporal `h2diag` agregado para diagnóstico y **removido antes del
  commit** (h2.c vuelve a su contrato sin logging, ADR-0009).

## Decisiones de seguridad tomadas o revisadas

- Claves privadas (machine/node) ahora viven en NVS en claro **en banco de pruebas
  sin flash encryption** — aceptado solo para desarrollo, consistente con el
  staging de ADR-0003. Antes de salir del banco: activar flash encryption Release
  + NVS encryption (el ADR ya lo fija).
- Ningún log imprime material de claves (regla §2.1 de AGENTS.md respetada durante
  todo el debugging, incluidos los dumps hex de records que solo se hicieron en
  builds desechables).
- Las auth keys rotaron dos veces durante la sesión; ambas quedaron expuestas en
  chat/capturas → **rotar la vigente al cerrar** (ver limpieza).
- Hallazgo de cppcheck (`uninitvar` sobre buffer `extra` del pushback): corregido
  inicializando el buffer, sin supresiones. cppcheck final: 0 hallazgos.

## Resultado

- Registro en producción: `RegisterResponse` con `"MachineAuthorized":true`.
- Nodo visible en admin con IP asignada confirmada por el usuario: **<ip-tailnet>**.
- Reinicio + reconexión reutiliza identidad (mismo nodo, sin nueva aprobación).
- Tests host 15/15 PASS; build ESP-IDF limpio (-Wall -Wextra -Werror).

## Pendiente / bloqueado

- El parser de netmap no encuentra la IP propia en el MapResponse compacto recibido
  (530 B sin `"100."` literal visible); la IP existe (confirmada en admin). Revisar
  forma exacta del MapResponse (posiblemente requiere poll posterior o campo
  `SelfNode` distinto) cuando se implemente el polling loop.
- Polling loop de map / reconexión (TODO explícito en `client_task`).
- Data plane WireGuard (disco, NAT traversal) — próximo hito mayor según ADR-0008.
- Flash encryption + Secure Boot v2 antes de cualquier despliegue fuera del banco.
