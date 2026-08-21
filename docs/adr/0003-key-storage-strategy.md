# ADR-0003: Estrategia de almacenamiento de claves en flash

- Estado: aceptado (confirmado por el usuario en el chat, 2026-08-20)
- Fecha: 2026-08-20
- Actualizado: 2026-08-20 — decisión de canal de provisioning inicial
- Actualizado: 2026-08-20 — resueltos NVS encryption, flash encryption, alcance de Secure Boot v2 y eFuses de debug (segunda sesión de trabajo)
- Actualizado: 2026-08-20 — revisado por primer dispositivo real (M5Stack Core 2, ESP32 clásico): esquema NVS unificado flash-enc-based y matizaciones por target (tercera pasada)

## Contexto

El ESP32 target no cuenta con elemento seguro dedicado en la variante base
(evaluar aparte, en un ADR posterior, si se adopta una variante con ATECC608
externo). Toda clave persistida en flash es en principio extraíble por alguien
con acceso físico y las herramientas correctas, salvo que se usen las
protecciones nativas del SoC. Ver AGENTS.md §2.1.

## Decisión

Los símbolos de configuración citados abajo fueron verificados contra la
documentación oficial de ESP-IDF (docs.espressif.com, sección Security y API
Reference de NVS Encryption) en la fecha de este ADR. La revisión de la tercera
pasada introduce **multi-target** (ver ADR-0004): validación primaria sobre
ESP32 clásico (M5Stack Core 2), secundaria sobre ESP32-C3. Los detalles que
difieren por chip se marcan explícitamente; los específicos del ESP32 clásico
se re-verifican contra su documentación propia al crear `sdkconfig.defaults`.

### NVS encryption — decidido

Se activa `CONFIG_NVS_ENCRYPTION` con esquema de protección de claves
**flash-encryption-based**, unificado para todos los targets:
`CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME` → `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC`.

Con este esquema, las claves XTS que cifran el contenido de NVS se almacenan
en una partición dedicada `nvs_keys` (tipo `data`, subtipo `nvs_keys`,
marcada `encrypted`, tamaño mínimo 4 KB en la tabla de particiones), generadas
on-chip por `nvs_flash_init()` y protegidas a su vez por flash encryption.
Esto cifra todo el namespace de credenciales: node key, auth key transitoria,
SSID/PSK Wi-Fi (que el driver Wi-Fi persiste en la NVS default).

Motivo de la revisión: el esquema HMAC-based propuesto inicialmente requiere
el periférico HMAC, **inexistente en el ESP32 clásico** — el SoC del primer
dispositivo real del proyecto (M5Stack Core 2). Ante la alternativa de
mantener dos esquemas según chip (dos caminos de código, dos superficies de
testeo, divergencia auditiva entre dispositivos de la flota), se unifica en
flash-enc-based, que funciona idéntico en ambos targets. El trade-off aceptado
explícitamente: todo el material secreto queda bajo una única raíz de
confianza (la clave de flash encryption en eFuse). Se considera aceptable
porque extraer esa clave eFuse read-protected exige exactamente la clase de
ataque físico sofisticado ya excluida del alcance v1 en ADR-0002.

Asignación de bloques eFuse (verificar con `idf.py efuse-summary` durante
provisioning): `BLOCK_KEY0` / bloque equivalente del target = clave de flash
encryption (generada en el primer boot, read/write-protected). Resto de
bloques libres; los que queden sin usar al cerrar un dispositivo como
producción se documentan en el checklist de despliegue.

### Flash encryption — decidido

Obligatoria en modo **Release** antes de cualquier salida del banco de pruebas.
Configuración de build:

- `CONFIG_SECURE_FLASH_ENC_ENABLED` (Enable flash encryption on boot).
- `CONFIG_SECURE_FLASH_ENCRYPTION_MODE` = Release.
- `CONFIG_SECURE_UART_ROM_DL_MODE` = "Permanently switch to Secure mode"
  (recomendación de Espressif; limita el ROM download mode a comandos básicos).

Algoritmo: difiere por target — en C3 es XTS-AES con clave de 256 bits; en
ESP32 clásico es AES-256 con tweak por bloque y el eFuse de control es
`FLASH_CRYPT_CNT` (un solo bit, no tres como `SPI_BOOT_CRYPT_CNT` del C3).
En ambos casos la clave la genera el propio chip en el primer boot (RNG
interno), queda en eFuse read/write-protected, y jamás es accesible por
software. Los nombres exactos de eFuses y símbolos del ESP32 clásico se
verifican contra su documentación específica al crear `sdkconfig.defaults`
(no se asumen por analogía con C3).

Procedimiento de activación (primer flasheo de un dispositivo):

1. Build con las opciones anteriores, flashear plaintext vía serial.
2. Primer boot: el bootloader genera la clave, cifra in-place bootloader,
   tabla de particiones, particiones `app` y toda partición marcada
   `encrypted` (puede tardar hasta un minuto). **No cortar alimentación
   durante este proceso** — interrumpirlo corrompe la flash.
3. En modo Release el bootloader quema `DIS_DOWNLOAD_MANUAL_ENCRYPT`,
   write-protege el eFuse de control (`SPI_BOOT_CRYPT_CNT` en C3,
   `FLASH_CRYPT_CNT` en ESP32 clásico), y deshabilita JTAG (ver sección de
   eFuses debug más abajo).

Irreversibilidad, explícita:

- Los eFuses quemados (eFuse de control write-protected,
  `DIS_DOWNLOAD_MANUAL_ENCRYPT`) son de un solo sentido. Un dispositivo en
  Release **no vuelve a Development**, y reflashear exige imágenes
  pre-cifradas con la clave (si se guardó copia en el host) u OTA.
- Seleccionar Release en menuconfig solo cambia el build; la transición real
  de un dispositivo ya en Development exige llamar
  `esp_flash_encryption_set_release_mode()` desde la aplicación (quema los
  eFuses) — está documentado que menuconfig por sí solo no quema nada.
- Recuperación de un brick post-Release es sustancialmente más limitada que en
  otros SoC del usuario (cf. recuperación de Orange Pi por SPI bootloader ya
  vivida): se asume que un dispositivo Release mal parado es descartable.
- Nunca reutilizar la misma clave de flash encryption entre dispositivos
  (best practice de Espressif; evita que ciphertext de un dispositivo sirva
  en otro).

Modo Development queda permitido **solo** dentro del banco de pruebas: deja la
flash legible-en-claro indirectamente (UART download mode puede cifrar al
vuelo), y ningún build Development sale del banco.

### Secure Boot v2 — decidido: pospuesto, no bloqueante para v1

No se activa en v1. El corte de capacidad del atacante físico de ADR-0002
(dump SPI sí, glitching no) hace que el valor agregado de SBv2 — impedir
ejecución de firmware no firmado ante un atacante capaz de reescribir flash —
apunte a un adversario por encima del caso base, mientras sus costos son
inmediatos: eFuses irreversibles, iteración de desarrollo más lenta, y
recuperación de brick prácticamente nula.

Disparadores explícitos de activación (basta uno):

1. Cualquier dispositivo sale del banco de pruebas hacia ubicación no
   controlada por el usuario.
2. Despliegue multi-dispositivo desatendido.
3. Antes de implementar cualquier mecanismo OTA (OTA sobre boot sin SBv2 abre
   ventanas de rollback/tampering que este ADR no cubre).
4. Cualquier indicio de intento de tampering físico sobre un dispositivo real.

Cuando se active, condiciones mínimas (registradas ahora para no improvisar):
RSA-3072 (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES`, clave de firma generada
offline con entropía de calidad y custodiada fuera de la máquina de build),
revocación de slots de digest no usados, y **orden de quemado respetado**:
las claves read-protected (flash encryption) deben estar quemadas *antes* de
activar SBv2, porque después de activarlo ESP-IDF bloquea nuevas
read-protecciones por default (`CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS`).
Notas de hardware: SBv2 existe en C3 desde revisión v0.3 (ECO3) y en ESP32
clásico desde revisión 3 (ECO3). El Core 2 lleva ESP32-D0WDQ6-**V3**, así que
ambos targets del proyecto cumplen; la restricción de compra queda fijada en
ADR-0004.

### eFuses de debug/JTAG y separación de builds — decidido

Dos builds claramente diferenciados, **nunca la misma imagen para ambos roles**:

| | Build desarrollo | Build producción |
|---|---|---|
| JTAG / USB-Serial-JTAG | Habilitado (debugging en banco) | Deshabilitado |
| Flash encryption | Off o Development | **Release** |
| Modo provisioning serial | Compilado (`#ifdef`) | Compilado fuera |
| Marcación | Banner de log `DEV BUILD` + sufijo de versión | Banner limpio |

Mecanismo de deshabilitación de JTAG en producción: con flash encryption
Release activa, el propio bootloader quema en el primer boot los eFuses de
JTAG (`DIS_PAD_JTAG`, `DIS_USB_JTAG`) junto con el resto de hardening — es el
comportamiento documentado default. La opción `CONFIG_SECURE_BOOT_ALLOW_JTAG`
está clasificada por Espressif como "potentially insecure option" para
ambientes de testing: **prohibida** en builds de producción de este proyecto.

Separación técnica de builds: `sdkconfig.defaults` (base común) +
`sdkconfig.dev` / `sdkconfig.prod` seleccionados vía `SDKCONFIG_DEFAULTS`,
de forma que producir la imagen equivocada exija override deliberado, no un
menuconfig olvidado. El detalle exacto de los archivos se implementa cuando se
creen (pendiente de aceptación de este ADR).

### Ciclo de vida de la auth key post-consumo — decidido

Complementa el canal serial ya decidido:

1. Tras registro exitoso contra el control plane, la auth key se **purga de
   NVS** (sobrescritura/borrado de la entrada) y toda copia en RAM se zeroiza
   con `mbedtls_platform_zeroize()` — no `memset()` plano, que los
   optimizadores pueden eliminar.
2. El firmware no reintenta automáticamente con la misma auth key en loop
   silencioso ante fallo de registro (ya decidido en la adenda de
   provisioning): reporta y espera intervención.
3. Ningún log imprime la auth key ni ningún secreto, completo ni truncado de
   forma reversible (AGENTS.md §2.1).

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

**Almacenamiento:**

- **NVS encryption con esquema HMAC-based**
  (`CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC`, claves XTS derivadas en runtime
  desde una clave HMAC en eFuse, sin partición `nvs_keys`). Fue la elección
  primaria de la segunda pasada: desacopla la confidencialidad de NVS de la
  clave de flash encryption (dos raíces de confianza en vez de una). Descartada
  en esta revisión por restricción de hardware dura: el periférico HMAC no
  existe en el ESP32 clásico del primer dispositivo real (M5Stack Core 2), y
  mantener dos esquemas según chip duplica caminos de código y de testeo en un
  proyecto que prioriza auditabilidad. Queda como opción futura si algún día
  una flota 100% C3/S3 justifica reabrir el trade-off vía ADR.
- **Cifrado a nivel de aplicación hecho a mano** sobre NVS en claro.
  Descartado de plano: reinventar cifrado sobre un SoC que ofrece esto
  nativamente es exactamente el tipo de decisión que AGENTS.md §6 y este
  proyecto existen para evitar.
- **Secure Boot v2 activo desde v1.** Descartado por ahora con los
  disparadores explícitos de arriba; no es "no lo vamos a hacer", es "no es
  lo primero". Reabrir con ADR cuando se dispare.
- **Un único build dev/prod con flags en runtime.** Descartado: un build que
  puede habilitar JTAG o provisioning serial por runtime-config es un build
  de producción con superficie de desarrollo latente. La separación debe ser
  en compile-time.

## Consecuencias de seguridad

Directas y centrales — este ADR define el mecanismo principal de mitigación
para el eje de amenaza física completo (AGENTS.md §2.1):

- Contra el caso base de ADR-0002 (dump SPI sin desoldar), el conjunto flash
  encryption Release + NVS encryption flash-enc-based deja al atacante con
  ciphertext sin clave accesible: la clave de flash encryption vive read/write
  protegida en eFuse y las claves NVS están cifradas bajo ella. Ningún secreto
  del proyecto queda en flash en claro. Trade-off declarado: es una única raíz
  de confianza — pero extraerla exige exactamente la clase de ataque físico
  sofisticado fuera de alcance v1, y la uniformidad entre targets reduce la
  superficie de error de implementación, que es el riesgo más real a esta
  altura del proyecto.
- La decisión de provisioning por serial reduce a cero la superficie remota
  expuesta durante el setup inicial, al costo de requerir acceso físico para
  provisionar cada dispositivo (trade-off aceptado explícitamente: en este
  proyecto el acceso físico al banco de pruebas ya es un supuesto base, no una
  limitación nueva).
- El radio de daño de una extracción exitosa queda acotado por diseño: una
  node key extraída compromete ese nodo hasta su revocación en la tailnet, no
  la tailnet completa; las ACLs sobre `tag:esp32-iot` recomendadas en
  AGENTS.md §2.3 son el complemento operativo de este acotamiento.
- Pospuesto SBv2, el riesgo residual declarado es: un atacante físico por
  encima del caso base podría intentar reemplazo de firmware. Mitigación
  parcial heredada igualmente del modo Release (`DIS_DOWNLOAD_MANUAL_ENCRYPT`
  impide flashear plaintext por UART); cierre completo recién con SBv2 cuando
  se disparen sus condiciones.

## Consecuencias de estabilidad

- Flash encryption: el primer boot cifra in-place y **no tolera corte de
  alimentación** durante el proceso — procedimiento de provisioning debe
  garantizar alimentación estable. Reflashear post-Release exige imágenes
  pre-cifradas u OTA; la iteración diaria sigue siendo en builds Development
  del banco de pruebas.
- Irreversibilidad de eFuses: un dispositivo mal parado en Release es, en la
  práctica, descartable. Documentado como tal para no sorprender en campo,
  especialmente dado que el usuario ya resolvió un boot failure de Orange Pi
  por SPI bootloader en otro proyecto — el mismo tipo de recuperación es
  sustancialmente más limitado o imposible en un ESP32 con estas protecciones
  activas.
- Esquema flash-enc de NVS: requiere partición `nvs_keys` presente y
  completamente vacía en el primer arranque que inicializa NVS (si contiene
  datos malformados, `nvs_flash_init()` falla con
  `ESP_ERR_NVS_CORRUPT_KEY_PART`) — el procedimiento de provisioning debe
  garantizar el erase inicial de esa partición. Un solo esquema para todos
  los targets significa un solo camino de código y una sola matriz de tests.
- Secure Boot v2 pospuesto elimina por ahora sus costos de estabilidad
  (bootloader más grande, verificación en cada boot, riesgo de brick por
  revocación agresiva de claves); activarlo vía disparador re-introduce esos
  costos conscientemente.
