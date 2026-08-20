# ADR-0003: Estrategia de almacenamiento de claves en flash

- Estado: propuesto (provisioning decidido y aceptado; resto pendiente)
- Fecha: 2026-08-20
- Actualizado: 2026-08-20 — decisión de canal de provisioning inicial

## Contexto

El ESP32 target no cuenta con elemento seguro dedicado en la variante base
(evaluar aparte, en un ADR posterior, si se adopta una variante con ATECC608
externo). Toda clave persistida en flash es en principio extraíble por alguien
con acceso físico y las herramientas correctas, salvo que se usen las
protecciones nativas del SoC. Ver AGENTS.md §2.1.

## Decisión (a completar en la primera sesión de implementación)

Pendiente de fijar con precisión:

- Activación de **NVS encryption** de ESP-IDF para el namespace donde se
  guarda la node key y cualquier credencial persistente.
- Activación de **flash encryption** en modo Release antes de cualquier
  despliegue fuera del banco de pruebas controlado del usuario. Documentar
  aquí el procedimiento exacto (`idf.py`, eFuses involucrados, e
  irreversibilidad del proceso una vez quemados los eFuses de producción).
- Alcance de **Secure Boot v2** para v1: bloqueante o pospuesto, con criterio
  explícito de qué evento dispara su activación (ej. el dispositivo deja el
  banco de pruebas).
- Estado de eFuses de debug/JTAG en builds de producción (deshabilitados) vs.
  builds de desarrollo (habilitados y marcados como tales, nunca la misma
  imagen para ambos casos).
- Manejo de la auth key de un solo uso durante el provisioning inicial: cómo
  llega al dispositivo, y confirmación explícita de que no queda persistida
  en flash una vez consumida y reemplazada por la node key propia.
  **Decidido (ver abajo): provisioning por serial/USB.**

### Canal de provisioning inicial — decidido

Se provisiona la auth key (y las credenciales Wi-Fi) por **serial/USB**, con
el dispositivo conectado físicamente al equipo del usuario en el banco de
pruebas controlado. Se descarta el AP Wi-Fi temporal de setup para v1.

Razonamiento: el AP de setup abre una ventana de red no autenticada en el
momento más sensible del ciclo de vida del dispositivo — antes de que exista
identidad propia (node key) y mientras la auth key todavía no se consumió.
Cualquier atacante en rango radioeléctrico durante esa ventana podría
intentar conectarse al AP o interceptar el intercambio. El canal serial
requiere acceso físico al banco de pruebas, perímetro que ya está bajo
control del usuario y consistente con el resto de las mitigaciones de
amenaza física (flash encryption, eFuses de debug). No tiene sentido cerrar
el eje físico con flash encryption mientras se abre un eje remoto nuevo
solo para el setup.

Mecanismo concreto (a implementar, no solo declarado):

1. Firmware de desarrollo expone un modo "provisioning" que solo escucha por
   UART/USB-serial, nunca levanta un AP ni un socket de red antes de tener
   Wi-Fi + auth key configurados.
2. El usuario inyecta Wi-Fi SSID/PSK y la auth key de un solo uso vía un
   script de host (a definir en una sesión posterior, vive fuera del
   firmware) que escribe sobre el puerto serial.
3. El firmware persiste esos valores en NVS (cifrada, ver el resto de este
   ADR) y, en el primer arranque exitoso con conectividad, consume la auth
   key contra el control plane de Tailscale, obtiene su node key propia, y
   **sobreescribe/purga la auth key de NVS** — no queda un valor reutilizable
   en flash después del primer registro exitoso.
4. Si el registro falla, el firmware no reintenta indefinidamente con la
   misma auth key en un loop silencioso — reporta el fallo (log serial en
   desarrollo) y espera intervención, para no quemar una auth key de un
   solo uso contra reintentos automáticos mal diseñados.
5. El modo "provisioning" por serial se compila condicionalmente y **no** se
   incluye en un build de producción con flash encryption + Secure Boot
   activos salvo que un ADR posterior defina un flujo de re-provisioning
   seguro para dispositivos ya desplegados (fuera de alcance v1).

Pendiente todavía en este ADR (no resuelto por esta decisión): NVS/flash
encryption exactas, alcance de Secure Boot v2, eFuses de debug.

## Alternativas consideradas

**Provisioning:**
- **AP Wi-Fi temporal (captive portal) para setup inicial.** Descartado para
  v1 por lo expuesto arriba: expande la superficie remota justo cuando el
  dispositivo es más vulnerable (sin identidad propia todavía). Podría
  reconsiderarse en un ADR posterior si aparece un caso de uso donde el
  acceso físico/serial no sea viable (ej. despliegue en ubicación remota sin
  posibilidad de traer el dispositivo al banco de pruebas primero).
- **BLE provisioning** (patrón común en IoT, ej. ESP-IDF `wifi_provisioning`
  sobre BLE). Descartado por ahora por la misma razón que el AP: superficie
  remota adicional antes de tener identidad propia, y agrega una pila
  Bluetooth completa a la superficie de ataque total del firmware sin
  necesidad clara en el caso de uso actual (banco de pruebas propio).
- **Hardcodear la auth key en el binario de un build "para este dispositivo
  específico".** Descartado de plano: contradice AGENTS.md §2.3 explícitamente
  (nunca se commitea ni se hardcodea una auth key real) y convierte cualquier
  dump de flash o filtración del binario en compromiso directo de la tailnet.

**Almacenamiento (pendiente, no resuelto todavía):**

Pendiente — se completa junto con el resto de la decisión de NVS/flash
encryption. Como mínimo debe compararse NVS encryption + flash encryption
nativos vs. cualquier esquema de cifrado de aplicación hecho a mano (este
último se descarta salvo justificación muy fuerte, ya que reinventar cifrado
a nivel de aplicación sobre un SoC que ya ofrece esto nativamente es
exactamente el tipo de decisión que este proyecto busca evitar).

## Consecuencias de seguridad

Directas y centrales — este ADR define el mecanismo principal de mitigación
para el eje de amenaza física completo (AGENTS.md §2.1). La decisión de
provisioning por serial reduce a cero la superficie remota expuesta durante
el setup inicial, al costo de requerir acceso físico para provisionar cada
dispositivo (trade-off aceptado explícitamente: en este proyecto el acceso
físico al banco de pruebas ya es un supuesto base, no una limitación nueva).

## Consecuencias de estabilidad

Flash encryption y Secure Boot v2 tienen implicancias operativas (ej.
dificultad de reflashear en desarrollo, irreversibilidad de eFuses quemados)
que deben quedar documentadas aquí para no sorprender en campo, especialmente
dado que el usuario ya resolvió un boot failure de Orange Pi por SPI bootloader
en otro proyecto — el mismo tipo de recuperación es sustancialmente más
limitado o imposible en un ESP32 con Secure Boot activo si algo sale mal.
