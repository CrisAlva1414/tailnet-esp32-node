# ADR-0007: Provisioning de credenciales por consola serie

- Estado: propuesto (esperando confirmación explícita del usuario para pasar a `aceptado`)
- Fecha: 2026-08-20

## Contexto

AGENTS.md §2.3 exige un ADR explícito antes de escribir cualquier código de
provisioning. Los datos a provisionar son: SSID/PSK del Wi-Fi y la auth key
de Tailscale (de un solo uso, expiración corta, tag `tag:esp32-iot`). El
usuario ya aportó ambos datos para el banco de pruebas, pero la auth key
circuló por chat: se registra la recomendación de revocarla y generar una
fresca en el momento del provisioning real.

## Decisión

### Canal único v1: consola serie USB

Provisionamiento exclusivamente por consola serie (CH9102/CP2104, 115200),
coherente con el canal decidido en ADR-0003 para el banco de pruebas
controlado. Comandos mínimos:

- `wifi set <ssid>` — pide la PSK por consola **sin echo**.
- `tskey set` — lee la auth key **sin echo**, valida el prefijo de formato
  (`tskey-auth-`), la persiste en NVS.
- `provision status` — muestra estado SIN secretos: qué hay cargado, no su
  valor (longitudes o huellas truncadas, nunca material sensible, §2.1).
- `provision wipe` — borra todas las credenciales provisionadas.

### Reglas de manejo de secretos

1. Ningún secreto se loguea ni se ecoa, ni en nivel debug (§2.1).
2. Input de secretos leído carácter a carácter con echo deshabilitado.
3. Auth key: un solo uso. Tras registro exitoso ante el control plane, se
   purga de NVS con borrado seguro en RAM (`mbedtls_platform_zeroize` antes
   de liberar). Si el registro falla, queda para reintento hasta expirar.
4. NVS namespace dedicado (`tsnode_prov`), separado de otros datos de app.
5. En reposo: encriptados solo cuando flash encryption + NVS encryption
   estén activos (ADR-0003). En builds de desarrollo del banco quedan en
   claro en flash — aceptado EXCLUSIVAMENTE en el banco; ninguna imagen así
   sale de ahí.

### Lo que NO se hace en v1

- Sin lockout/rate-limit de la consola: requiere acceso físico para
  explotarse, y ese vector está cubierto en producción por el cifrado de
  reposo (quien tenga la flash no obtiene los secretos en claro). Se
  re-evalúa si aparece un escenario de acceso físico temporal hostil.
- Sin provisioning inalámbrico de ninguna forma en v1.

## Alternativas consideradas

- **Hardcodear credenciales en firmware**: NUNCA — prohibido por §2.3,
  termina en el repo o en el binario distribuido.
- **SoftAP + portal cautivo**: descartado en v1 — abre una superficie
  inalámbrica sin autenticación fuerte durante la ventana de setup. Puede
  re-evaluarse con su propio modelo de amenaza si algún día hace falta
  provisioning sin una PC cerca.
- **BLE provisioning**: descartado en v1 — superficie y tooling extra para
  el mismo resultado que la consola, con más código auditable.

## Consecuencias de seguridad

- La exposición de secretos queda acotada a dos momentos: tipeo por consola
  y reposo en NVS. Ningún canal de red participa en el provisioning v1.
- Registrado explícitamente: la primera auth key aportada por el usuario
  circuló por chat y DEBE revocarse; el flujo correcto es generar una nueva
  en el momento de registrar el nodo.
- La validación de formato de la auth key es sintáctica (prefijo), no una
  verificación de validez ante el control plane — esa solo ocurre al
  registrar, que es donde la key se consume.

## Consecuencias de estabilidad

- Flujo simple sin radios extra activos durante setup: menos estados, menos
  superficies de crash.
- Validar formato antes de persistir evita credenciales corruptas por typos;
  `provision status` permite verificar estado sin exponer valores.
