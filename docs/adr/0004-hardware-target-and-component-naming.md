# ADR-0004: Target de hardware y nombre del componente

- Estado: propuesto (esperando confirmación explícita del usuario para pasar a `aceptado`)
- Fecha: 2026-08-20

## Contexto

La sesión de inicialización usó dos supuestos de trabajo sin confirmar:
ESP32-C3 como target de hardware y `tsnode` como nombre provisional del
componente propio (`components/tsnode/`, prefijo `tsnode_*` ya fijado en
`docs/format/c-style.md`). AGENTS.md §9 exige que ambos queden confirmados en
un ADR antes de escribir código real. Este ADR los fija o los reemplaza.

## Decisión

### Target de hardware: ESP32-C3, revisión de chip ≥ v0.3 (ECO3)

Se confirma ESP32-C3 (RISC-V, single core 160 MHz, Wi-Fi 4) como target de v1,
con una restricción explícita de compra: **revisión de chip v0.3 (ECO3) o
superior**. La restricción no es capricho:

- Secure Boot v2 solo existe en C3 desde revisión v0.3. ADR-0003 pospone SBv2
  pero con disparadores concretos de activación; comprar chips que nunca
  podrán activarlo convertiría ese "pospuesto" en "imposible".
- El debugging por el USB-Serial/JTAG integrado también requiere revisión
  ≥ v0.3, y ese puerto USB integrado es parte del canal serial/USB decidido
  en ADR-0003 para provisioning.

Razones de la elección del C3 para este caso de uso:

1. **USB-Serial/JTAG nativo**: el canal de provisioning serial no necesita
   puente USB-UART externo; menos componentes, menos superficie.
2. **Aceleración criptográfica por hardware** (AES, SHA, RSA, HMAC, firma
   digital): ataca directamente el modo de falla documentado del proyecto de
   referencia (watchdog resets por timing de crypto) — el presupuesto de
   tiempo de crypto deja de depender de software optimizado a mano.
3. **Footprint y RAM suficientes** para un cliente mínimo disciplinado
   (AGENTS.md §5.4), al menor costo y consumo de las alternativas.
4. **Disponibilidad**: placas dev comunes y baratas (ESP32-C3-DevKitM-1 y
   clones tipo supermini), fáciles de reponer para el banco de pruebas.

Implicancia declarada del single core: las operaciones crypto bloquean el
scheduler mientras corren. Aunque la aceleración por hardware minimiza la
ventana, la medición del presupuesto de tiempo real exigida por AGENTS.md
§2.2 (nada de "ajustar el número hasta que deje de resetear") aplica desde el
primer módulo que use crypto.

### Nombre del componente: `tsnode` (se confirma, deja de ser provisional)

El componente vive en `components/tsnode/` con prefijo público `tsnode_*`,
tal como ya está fijado en `docs/format/c-style.md`. No se renombra: el
nombre es corto, no colisiona con bibliotecas conocidas del dominio (la
librería Go de Tailscale se llama `tsnet`; `tsnode` es distinto), y el costo
de renombrar ahora (directorio + convención de naming + headers futuros) no
compra nada. La estructura existente queda validada tal cual.

## Alternativas consideradas

**Hardware:**

- **ESP32-S3**: dual core, más RAM, USB OTG. Descartado para v1: nada de esto
  lo necesita este caso de uso (sin BLE, sin periféricos pesados), cuesta más,
  consume más, y agrega variantes/matriz de testing sin beneficio. Queda como
  opción natural si un futuro caso de uso exige más margen.
- **ESP32 clásico** (Xtensa, dual core): descartado. Sin USB nativo (exige
  puente UART externo justo para el canal de provisioning), periféricos crypto
  más viejos, plataforma madura pero en camino de reemplazo.
- **ESP32-C6**: Wi-Fi 6 + 802.15.4 (Zigbee/Thread). Descartado: este proyecto
  no usa nada de eso; soporte en ESP-IDF más joven significa menos historial
  de campo para las features de seguridad exactas que usamos (flash encryption
  Release, NVS HMAC). Reevaluable sin costo si C6 madura antes de comprar
  hardware.

**Nombre:**

- `tailnode`, `tsclient`, `tslite`: descartados — más largos o más ambiguos,
  sin ninguna ventaja sobre el nombre ya extendido por el repo.

## Consecuencias de seguridad

- Fija el conjunto de primitivas crypto por hardware disponibles (AES/SHA/RSA/
  HMAC en C3), que condiciona el ADR futuro de arquitectura de protocolo y la
  elección mbedTLS-vs-alternativa de AGENTS.md §6.
- La restricción de revisión ≥ v0.3 garantiza que Secure Boot v2 permanezca
  *disponible* como mitigación futura (ADR-0003), aunque pospuesta.
- Ningún cambio inmediato en superficie de ataque: es una decisión de qué
  chip corre el modelo de amenaza ya fijado, no de qué amenazas existen.

## Consecuencias de estabilidad

- Single core: crypto bloquea scheduler; obliga a medir presupuesto de tiempo
  real desde el primer módulo (AGENTS.md §2.2), no a posteriori. La
  aceleración por hardware reduce el riesgo heredado del proyecto de
  referencia (watchdog resets por crypto lenta).
- Revisión de chip ≥ v0.3 evita de paso erratas conocidas de revisiones
  tempranas del C3 (el motivo por el que Espressif exige ECO3 para SBv2 y
  USB-JTAG).
- Confirmar el target permite fijar versión de ESP-IDF y `idf.py set-target`
  definitivos en `sdkconfig.defaults` cuando se cree (pendiente de aceptación
  de ADR-0003 y este ADR).
