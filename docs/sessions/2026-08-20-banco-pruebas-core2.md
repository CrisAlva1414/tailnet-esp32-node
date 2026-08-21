# 2026-08-20 — banco de pruebas: Core 2 en USB, ESP-IDF local y primer flasheo

## Contexto

El usuario conectó el M5Stack Core 2 por USB y pidió tomarlo como banco de
pruebas, verificando qué modos de debugging son posibles. Confirmó además que
el firmware preexistente en la placa (`homebot-firmware v0.1.0`, con JWT y
credenciales Wi-Fi en NVS) es un proyecto deprecated y autorizó borrarlo.

## Cambios

- Sin cambios de código del proyecto. Cambios de entorno de trabajo:
  - Detectado el dispositivo: CH9102F en `/dev/ttyACM0` (permisos OK,
    usuario en `dialout`). Chip verificado vía esptool: **ESP32-D0WDQ6-V3
    rev v3.1**, 16 MB flash, MAC `2c:bc:bb:82:19:d4`.
  - Instalado **ESP-IDF v5.5** local desde la fuente oficial
    (`github.com/espressif/esp-idf`, tag v5.5, clone shallow con
    submodules) en `~/esp/esp-idf`, toolchains para los 4 targets Tier 1.
    Misma versión exacta que CI — cero divergencia de toolchain.
  - Prerrequisitos instalados sin root vía pip: `cmake 3.31.10`
    (fijada <4 por compatibilidad de políticas) y `ninja`. `sudo` requiere
    contraseña no disponible para el agente.
- Primer ciclo completo en hardware: `idf.py set-target esp32 && build` →
  `erase-flash` (borra homebot + credenciales, autorizado) → `flash` →
  captura serie con reset por RTS/DTR.
- Verificado en vivo: `tsnode_init -> TSNODE_OK`, estado `INITIALIZED`,
  `tsnode_start -> TSNODE_ERR_NOT_IMPLEMENTED` (bloqueo por ADRs, diseño).

## Decisiones de seguridad tomadas o revisadas

- Borrado del firmware deprecated con sus secretos (JWT en NVS namespace
  `ruki`, SSID/PSK): autorizado explícitamente por el usuario en el chat.
  La placa queda dedicada al banco de pruebas de este proyecto.
- Build flasheado es Development sin flash encryption ni SBv2: válido SOLO
  en este banco (ADR-0003). No salir de acá con esta imagen.
- Modos de debugging relevados: monitor serie ✓ (en uso), gdbstub UART
  posible más adelante vía config, JTAG real NO disponible en esta placa
  (sin pines expuestos, ADR-0004); si se necesitara breakpoints completos,
  adaptador externo ESP-PROG (~USD 15) cableado a test points.

## Pendiente / bloqueado

- Primer ítem concreto para `sdkconfig.defaults` (sigue gating por
  aceptación de ADR-0003): fijar `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` — hoy el
  binario declara 2MB default y el boot lo advierte contra el chip real.
- Confirmación del usuario sobre ADRs 0002–0006 + desviación §4 + layout
  `apps/`: sigue bloqueando protocolo real.
- Siguiente hito de banco: provisioning serial de auth key (requiere
  ADR-0003 aceptado) y prueba Wi-Fi mínima en `main.c`.

## Nota de entorno

Para usar el toolchain en shells nuevos:
`. ~/esp/esp-idf/export.sh` antes de `idf.py`.
