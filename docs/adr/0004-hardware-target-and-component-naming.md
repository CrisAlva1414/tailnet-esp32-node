# ADR-0004: Target de hardware y nombre del componente

- Estado: propuesto (esperando confirmación explícita del usuario para pasar a `aceptado`)
- Fecha: 2026-08-20
- Actualizado: 2026-08-20 — revisado por primer dispositivo real: M5Stack Core 2 (ESP32 clásico) como target primario de validación, ESP32-C3 pasa a secundario

## Contexto

La sesión de inicialización usó dos supuestos de trabajo sin confirmar:
ESP32-C3 como target de hardware y `tsnode` como nombre provisional del
componente propio (`components/tsnode/`, prefijo `tsnode_*` ya fijado en
`docs/format/c-style.md`). AGENTS.md §9 exige que ambos queden confirmados en
un ADR antes de escribir código real.

En la tercera pasada el usuario informó el primer dispositivo a desplegar:
un **M5Stack Core 2** con caso de uso de **intercomunicador**. El Core 2 no
lleva un ESP32-C3: lleva un **ESP32-D0WDQ6-V3** (ESP32 clásico, Xtensa LX6
dual core 240 MHz), verificado contra la documentación oficial de M5Stack.
Este ADR reemplaza el supuesto de target único C3 por una estrategia
multi-target con validación primaria en el hardware que realmente se va a
desplegar primero.

## Decisión

### Estrategia de targets

El componente `tsnode` se desarrolla **multi-target** sobre ESP-IDF (el costo
de compilar para varios targets de la misma familia es bajo a nivel
componente), con esta jerarquía explícita:

| Target | SoC | Rol |
|---|---|---|
| **Primario v1** | ESP32 clásico — M5Stack Core 2 (ESP32-D0WDQ6-V3) | Validación y primer despliegue real (intercomunicador) |
| Secundario | ESP32-C3, revisión ≥ v0.3 (ECO3) | Soportado por el componente; se valida cuando exista hardware |

"Primario" significa: cada cambio se compila y prueba primero aquí; CI
compila ambos, pero los test vectors y pruebas en hardware real corren contra
el primario. "Secundario" significa: debe seguir compilando y sus diferencias
de plataforma quedan aisladas detrás de abstracciones del componente, pero no
se declara validado hasta tener una placa en el banco.

### Target primario: M5Stack Core 2

Especificaciones relevantes (fuente: docs.m5stack.com):

- SoC ESP32-D0WDQ6-V3: Xtensa LX6 dual core 240 MHz, 520 KB SRAM.
- 16 MB flash, 8 MB PSRAM — margen holgado para buffers de audio y crypto.
- USB-C vía puente CP2104/CH9102F hacia UART0 (según revisión de placa).
- Periféricos de audio: mic PDM/I2S SPM1423, amplificador NS4168 + speaker.
- Pantalla táctil 2.0" 320x240 (ILI9342C + FT6336U), RTC BM8563, PMIC AXP192.

Razones de la elección como primario:

1. Es el dispositivo que el usuario va a desplegar primero (intercomunicador):
   validar contra el hardware real evita optimizar para un banco de pruebas
   abstracto.
2. La revisión V3 (ECO3) cumple la restricción que ya habíamos fijado:
   Secure Boot v2 existe en ESP32 clásico desde ECO3, así que la mitigación
   pospuesta en ADR-0003 permanece disponible también en este chip.
3. El hardware de audio y pantalla del caso de uso intercom está todo a bordo;
   la PSRAM permite buffers de audio sin presionar el heap interno (relevante
   para la regla anti-fragmentación de AGENTS.md §4).

Implicancias declaradas del cambio de chip:

- **Provisioning serial**: sigue siendo el canal decidido en ADR-0003; cambia
  el puente (CP2104/CH9102F externo en vez de USB-Serial/JTAG nativo del C3).
  Mismo flujo, otro driver del host.
- **Debug JTAG**: la placa Core 2 no expone los pines JTAG (documentación de
  terceros coincide: debugging JTAG no soportado por limitaciones de pinout).
  El debugging en banco será por logs UART; los eFuses de debug de ADR-0003
  aplican igual a nivel chip.
- **Single core vs dual core**: a diferencia del C3, el ESP32 clásico tiene
  dos núcleos — el núcleo de protocolo puede convivir con tareas de audio/UI
  sin bloquear el scheduler. No se diseña asumiendo eso prematuramente: la
  medición de presupuesto de tiempo de AGENTS.md §2.2 sigue aplicando, ahora
  con un factor de complejidad extra (contención entre núcleos, prioridades
  FreeRTOS de audio vs. crypto/red).

### Alcance funcional nuevo declarado: intercomunicador

El caso de uso intercomunicador agrega un dominio de aplicación (captura y
playback de audio sobre la tailnet) que excede al cliente Tailscale mínimo de
AGENTS.md §1. Se registra el límite: `tsnode` sigue siendo solo el cliente
Tailscale (transporte); la capa de audio/UI/dispositivo vive fuera del
componente, en `main/` o componentes de aplicación separados, y su diseño
(push-to-talk vs. mic siempre activo, codec, indicador visual de estado del
micrófono) requiere su propio ADR antes de implementarse. Nota de modelo de
amenaza que ese ADR deberá formalizar: un intercom comprometido es escucha
del espacio físico donde está instalado — el impacto de compromiso supera al
de un sensor IoT genérico.

### Nombre del componente: `tsnode` (se confirma, deja de ser provisional)

El componente vive en `components/tsnode/` con prefijo público `tsnode_*`,
tal como ya está fijado en `docs/format/c-style.md`. No se renombra: el
nombre es corto, no colisiona con bibliotecas conocidas del dominio (la
librería Go de Tailscale se llama `tsnet`; `tsnode` es distinto), y el costo
de renombrar ahora (directorio + convención de naming + headers futuros) no
compra nada. La estructura existente queda validada tal cual.

## Alternativas consideradas

**Hardware:**

- **Mantener ESP32-C3 como único target** (versión anterior de este ADR).
  Descartado: significaba validar contra hardware distinto del que se va a
  desplegar, y obligaba a descartar el Core 2 ya en posesión del usuario.
- **ESP32-S3**: dual core con más RAM y USB nativo. Descartado para v1: nada
  del caso de uso lo exige hoy, y el Core 2 ya cubre audio + UI. Queda como
  opción natural si un futuro dispositivo lo justifica.
- **Descartar el Core 2 y comprar placas C3**: descartado — el Core 2 es el
  dispositivo elegido por el usuario para el intercomunicador; el proyecto
  sirve al caso de uso, no al revés.

**Nombre:**

- `tailnode`, `tsclient`, `tslite`: descartados — más largos o más ambiguos,
  sin ninguna ventaja sobre el nombre ya extendido por el repo.

## Consecuencias de seguridad

- Multi-target introduce el riesgo de divergencia de configuración de
  seguridad entre chips (eFuses distintos, símbolos distintos). Mitigación:
  ADR-0003 marca explícitamente qué detalles difieren por target y exige
  verificación contra documentación específica al crear `sdkconfig.defaults`;
  CI compila ambos targets para que ninguna divergencia pase inadvertida.
- SBv2 permanece disponible en ambos targets (ambos ECO3+): la restricción de
  compra del C3 se mantiene para futuras placas.
- El caso de uso intercomunicador eleva el impacto de un compromiso de nodo
  (micrófono en espacio físico). El modelo de amenaza de ADR-0002 no cambia,
  pero el ADR de aplicación de audio deberá tratar ese impacto explícitamente
  (estado visible del mic, diseño push-to-talk por defecto salvo decisión
  contraria justificada).
- Ningún cambio inmediato de superficie: es una decisión de qué chip corre el
  modelo de amenaza ya fijado, no de qué amenazas existen.

## Consecuencias de estabilidad

- Dual core del ESP32 clásico reduce el riesgo heredado del proyecto de
  referencia (watchdog resets por crypto lenta) respecto del single-core C3,
  pero agrega contención por periféricos compartidos (I2S, SPI de flash) entre
  audio y red — se mide, no se asume.
- 16 MB flash / 8 MB PSRAM dan margen holgado; el riesgo de fragmentación de
  heap interno se mitiga poniendo buffers grandes de audio en PSRAM.
- Compilar multi-target en CI desde el día uno evita descubrir
  incompatibilidades de plataforma tarde.
- Confirmar los targets permite fijar versión de ESP-IDF y `idf.py set-target`
  definitivos en `sdkconfig.defaults` cuando se cree (pendiente de aceptación
  de ADR-0003 y este ADR).
