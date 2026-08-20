# ADR-0002: Modelo de amenaza aceptado para v1

- Estado: propuesto (completo, esperando confirmación explícita del usuario para pasar a `aceptado`)
- Fecha: 2026-08-20
- Actualizado: 2026-08-20 — completado en la segunda sesión de trabajo

## Contexto

AGENTS.md §2 fija los lineamientos generales de modelo de amenaza para este
proyecto: amenaza física (dispositivo con acceso físico, extracción de flash) y
amenaza remota (protocolo, red, MITM, replay) se tratan con **igual prioridad**,
sin que una se optimice a costa de la otra sin justificación explícita.

Este ADR formaliza esas líneas generales en decisiones concretas antes de
escribir el primer módulo que toque red o almacenamiento persistente. Los
supuestos de capacidad del atacante definidos aquí determinan directamente:

- Si Secure Boot v2 es bloqueante para v1 o se pospone (resuelto en ADR-0003
  en base al corte físico de este ADR).
- Cómo se valida todo lo que llega por UDP **antes** del handshake Noise.
- Qué ancla la identidad del control plane de Tailscale (TLS vs. pinning).

## Decisión

### 1. Activos protegidos en reposo

Se protege, como mínimo, todo lo siguiente (cifrado en reposo, ver ADR-0003
para el mecanismo):

1. **Node key**: la clave privada WireGuard que identifica al dispositivo en
   la tailnet. Es el activo de mayor valor: su extracción permite suplantar
   al dispositivo ante sus peers hasta que el nodo sea removido de la tailnet.
2. **Machine key / clave de identidad estable**: la clave privada de largo
   plazo usada por el handshake ts2021/Noise ante el control plane. Su rol y
   formato exacto se confirmarán contra fuentes primarias de Tailscale en el
   ADR de arquitectura de protocolo; se lista como activo desde ya porque
   *toda* clave privada persistida lo es, sin excepción.
3. **Auth key de registro**, únicamente durante la ventana entre provisioning
   serial y consumo exitoso contra el control plane. Después del registro
   exitoso se purga de NVS y se zeroiza en RAM (ver ADR-0003); no es un activo
   en reposo de un dispositivo ya registrado.
4. **Wi-Fi PSK** (y SSID asociado). El driver Wi-Fi de ESP-IDF persiste SSID y
   passphrase en la partición NVS por defecto; documentación de Espressif cita
   explícitamente esto como motivación de NVS encryption.

No son activos en reposo: claves efímeras de Noise, claves de sesión TLS, y
material derivado que solo existe en RAM. Este último igual está alcanzado por
la regla de zeroización de ADR-0003 (los compiladores pueden optimizar away un
`memset` sobre memoria liberada).

La lista anterior se considera **completa para este diseño concreto**: v1 no
persiste nada más que credenciales Wi-Fi, auth key transitoria, node key y
clave(s) de identidad estable(s). Si una feature futura agrega un secreto
persistido nuevo, este ADR se revisa (ver Consecuencias de seguridad).

### 2. Atacante físico: supuesto de capacidad

**Caso base realista**: atacante con acceso físico sostenido al dispositivo
apagado o encendido, capaz de hacer **dump de flash SPI sin desoldar**
(pinza SOIC + programador USB de bajo costo, herramientas de decenas de USD,
sin laboratorio). Se asume además que puede identificar el hardware, usar
`esptool`/`espefuse`, y leer documentación pública de Espressif.

**Fuera de alcance v1, explícitamente**: glitching de voltaje para bypasear
verificaciones, ataques side-channel sobre eFuses, decapping, fault injection,
y en general toda clase de ataque que requiere equipamiento de laboratorio,
skill especializado y múltiples intentos destructivos.

Justificación del corte: el valor del activo (acceso a una tailnet personal
doméstica) es órdenes de magnitud menor al costo de esa clase de ataque, y las
propias mitigaciones de Espressif (flash encryption, Secure Boot) están
diseñadas y documentadas contra el primer caso, no contra el segundo. Prometer
protección anti-glitching sería prometer algo que ni el fabricante promete.

**Corolario declarado**: un atacante con posesión física del dispositivo
siempre puede interactuar con él *encendido* (oracle). Lo que este modelo
mitiga es la **extracción offline de secretos** y la **ejecución de firmware
no autorizado persistente**, no la posesión interactiva de un dispositivo con
build de desarrollo sin proteger. De ahí la separación estricta de builds dev/
prod de ADR-0003: un build de desarrollo dejado fuera del banco de pruebas
está fuera de los supuestos de este modelo.

### 3. Atacante remoto: supuesto de capacidad

**Caso base realista**: atacante que comparte la **misma red Wi-Fi local** que
el ESP32 durante operación normal — no solo durante provisioning, que ya quedó
resuelto por serial/USB en ADR-0003. Esto incluye capacidad de:

- ARP spoofing / DHCP spoofing / DNS spoofing en la LAN.
- Escanear puertos e inyectar, modificar, replayar o descartar paquetes UDP
  dirigidos al puerto WireGuard del dispositivo.
- Enviar input malformado a cualquier parser que corra **antes** de que exista
  autenticación (mensajes de handshake Noise/ts2021 incluidos). Todo parser
  pre-autenticación se diseña asumiendo este adversario (AGENTS.md §4).

**MITM hacia el control plane de Tailscale: NO asumido.** La identidad de
`login.tailscale.com` / `controlplane.tailscale.com` se ancla en la PKI
pública mediante TLS con validación estricta vía mbedTLS: verificación de
cadena completa contra roots públicos, verificación de hostname obligatoria,
y ningún código de skip-verify/insecure compilado en ninguna configuración de
build. No se implementa cert pinning en v1: el beneficio marginal frente a
este modelo (que no incluye compromiso de CA pública) no justifica la fragilidad
operativa de rotación de certificados contra dispositivos embebidos en campo.
Disparador de revisión: si en algún momento se decide cubrir compromiso de CA
o se detecta que Tailscale cambia su anclaje de confianza, eso es ADR nuevo.

Consecuencias operativas de este supuesto, para que queden escritas:

- **DNS spoofing no rompe confidencialidad** porque el ancla es el certificado
  TLS, no la resolución DNS. Un DNS hostil degrada disponibilidad, no seguridad.
- **La fuente de tiempo inicial no es confiable**: el ESP32 arranca sin reloj
  y necesita SNTP antes de poder validar certificados. Un atacante LAN puede
  servir tiempo falso (hacia atrás: riesgo de aceptar certificados expirados;
  hacia adelante: fallo de validación). Política: **fail-closed** — no se
  inicia TLS si el reloj está fuera de una ventana plausible respecto a la
  fecha de build del firmware; tiempo persistentemente implausible es error
  fatal reportado, no condición para relajar validación.

### 4. Fuera de alcance v1, explícito

| Amenaza | Por qué es razonable excluirla |
|---|---|
| Cadena de suministro del propio chip (silicio malicioso, backdoors en ROM de ESP32) | Inauditible para este proyecto; mitigarla exige otra clase de procurement completa. El proyecto no puede verificar ni comprometerse con lo que no puede inspeccionar. |
| Compromiso de la infraestructura Tailscale SaaS | Fuera de control propio. Blast radius acotado por diseño: el control plane solo ve claves públicas; la node key privada se genera en el dispositivo y nunca lo abandona. Comprometer SaaS no da claves privadas de nodos. |
| Ataques físicos sofisticados (glitching, side-channel, decapping) | Ver §2 de este ADR: costo muy superior al valor del activo. |
| Disponibilidad general (jamming RF, DoS de red, deauth masivo) | El modelo cubre confidencialidad e integridad. Disponibilidad es limitación conocida heredada también del corte sin-DERP: sin ruta directa, el nodo no conecta y lo reporta. |
| Side channels en runtime (análisis de consumo/emisiones con el dispositivo operando en entorno hostil) | Misma clase de costo/laboratorio que glitching. |

## Alternativas consideradas

- **Asumir atacante físico con capacidad de glitching desde v1.** Descartado:
  obligaría a tratar Secure Boot v2 como bloqueante inmediato, con costos de
  irreversibilidad y recuperación (ADR-0003) que no se justifican mientras los
  dispositivos vivan en el banco de pruebas del usuario. Queda como disparador
  explícito de revisión, no como supuesto base.
- **No asumir LAN hostil** (confiar en que la red doméstica del usuario es
  confiable). Descartado: contradice AGENTS.md §2.2 (input hostil por defecto)
  y es el supuesto que históricamente produce parsers frágiles "porque en mi
  red no pasa nada". Además IoT doméstico real convive con invitados, gadgets
  de terceros y cámaras baratas en la misma red.
- **Cert pinning hacia el control plane desde v1.** Descartado por fragilidad
  operativa (rotación de certs de Tailscale rompería dispositivos desplegados)
  frente a un beneficio que este modelo de amenaza no necesita. Documentado
  como decisión reversible vía ADR futuro.
- **Asumir MITM activo con capacidad de firmar como CA** (compromiso de CA
  pública). Descartado: misma clase de exclusión que cadena de suministro —
  cubrirlo exigiría pinning o TOFU con sus propios riesgos, sin que el modelo
  de amenaza personal/doméstico lo demande.

## Consecuencias de seguridad

Este ADR *es* la formalización del eje de seguridad del proyecto; no aplica
completarlo como si fuera un ADR más entre otros. Efectos concretos:

- Fija el adversario de referencia para todo test de fuzzing y toda decisión de
  parsing: LAN hostil con acceso completo a L2/L3 local, pre-autenticación.
- Determina que Secure Boot v2 no es bloqueante v1 (desarrollo completo en
  ADR-0003) y define los disparadores que lo activarían.
- Define el ancla de confianza hacia el control plane (PKI pública, validación
  estricta, sin pinning) y la política fail-closed de tiempo para TLS.
- Debe revisarse cada vez que se agregue una superficie nueva (DERP,
  provisioning Wi-Fi, OTA, un secreto persistido nuevo): cada una de esas
  features cambia quién puede atacar qué, y este documento debe reflejarlo
  antes de que exista código.

## Consecuencias de estabilidad

Ninguna directa: este ADR no introduce código. Indirectas relevantes:

- La política fail-closed de tiempo puede dejar al dispositivo sin conectividad
  si el entorno de red impide obtener hora plausible (SNTP bloqueado). Es un
  trade-off aceptado: prefiero no-conecta-reporta a conecta-inseguro.
- El supuesto de LAN hostil obliga a límites de validación estrictos en parsers
  pre-autenticación, que son también el lugar clásico de crashes por input
  malformado: los tests de límites que exige AGENTS.md §4 salen directamente
  de este ADR.
