# ADR-0006: Arquitectura en capas de `tsnode` y familia de targets soportados

- Estado: aceptado (confirmado por el usuario en el chat, 2026-08-20)
- Fecha: 2026-08-20

## Contexto

El usuario expandió el alcance: el M5Stack Core 2 es el primer dispositivo
(intercomunicador), pero la librería debe servir para **todo tipo de ESP32**
en tareas de automatización y domótica. ADR-0005 ya fijó `tsnode` como unidad
de reúso; falta decidir cómo se estructura internamente para que el mismo
componente sirva en variantes de chip distintas sin duplicar lógica de
protocolo, y qué variantes se prometen soportar. Se decide antes del primer
módulo real de protocolo porque determina dónde vive cada línea de código.

## Decisión

### Capas

1. **App** — fuera del componente. En este repo es `main/` (app de referencia
   mínima); en proyectos consumidores será su propio código (audio/UI del
   intercomunicador, sensores, relés). Solo ve la API pública.
2. **API pública** (`include/`) — tipos estables, errores, ciclo de vida.
   Sin fugas de tipos internos ni de plataforma (ningún header de ESP-IDF se
   incluye desde `include/`).
3. **Core** (`src/`, excepto `port/`) — máquina de estados, y en el futuro
   protocolo ts2021 y wrappers de crypto. C puro: **nunca incluye headers de
   ESP-IDF/FreeRTOS/lwIP**. Esto lo hace compilable en host contra mocks del
   port → los tests unitarios con `-Wpedantic` completo (compensación
   pactada en `docs/format/c-style.md`) corren sobre el mismo código lógico
   que ejecuta el dispositivo.
4. **Port** (`src/port/`) — interfaz interna `tsnode_port.h` por la que pasa
   TODO acceso a plataforma: entropía, tiempo, almacenamiento persistente,
   sockets UDP. Implementaciones: `src/port/esp_idf/` hoy; mock host en
   `tests/unit/` mañana.
5. **Regla dura verificada en CI**: nada bajo `src/` fuera de
   `port/esp_idf/` puede incluir headers de ESP-IDF. El guard falla el build
   si se viola — la arquitectura no depende de disciplina manual.

### Familia de targets

- **Tier 1** (CI obligatorio en cada push, soporte prometido): `esp32`
  (incluye M5Stack Core 2), `esp32c3`, `esp32s3`, `esp32c6`. Cubre ambas
  ISAs (Xtensa, RISC-V) y ambas generaciones de silicio.
- **Tier 2** (sin CI dedicado ni hardware aún; se promueven a Tier 1 cuando
  exista dispositivo real que los justifique): `esp32s2`, `esp32c2`.
- **Excluidos**: `esp32h2` — sin Wi-Fi, sin transporte IP nativo, incompatible
  con el modelo Tailscale de este proyecto salvo ADR futuro que justifique
  explícitamente escenarios 802.15.4.
- **Paridad de seguridad verificable en todo Tier 1**: flash encryption,
  NVS encryption con el esquema flash-enc-based de ADR-0003 (confirmado
  portable a toda la familia: el esquema HMAC-based requiere un periférico
  HMAC ausente en ESP32 clásico), Secure Boot v2 (el clásico exige chip
  ECO3+; el Core 2 usa ESP32-D0WDQ6-V3 ✓). Ninguna variante Tier 1 queda
  con claves menos protegidas que otra.
- **Piso de RAM**: la variante más chica de Tier 1 (C3, ~400 KB SRAM) marca
  el presupuesto. Ningún módulo puede asumir PSRAM. La validación de headroom
  de heap bajo carga criptográfica sostenida es requisito para declarar
  soporte real de cada target (disciplina §2 de AGENTS.md sobre watchdog y
  timing).

### Layout futuro multi-app (propuesto, NO ejecutado en esta sesión)

Cuando exista el segundo proyecto consumidor: `apps/<dispositivo>/` como
proyectos ESP-IDF independientes que consumen `components/tsnode` vía copia
vendorizada o submodule (etapas 1–2 del canal de ADR-0005). Este repo
mantiene `main/` como app de referencia mientras tanto. Implica actualizar
§3 de AGENTS.md — requiere ADR aceptado + confirmación explícita del usuario
(§11).

## Alternativas consideradas

- **Core compilando directo contra ESP-IDF, sin capa port.** Descartado:
  acopla la lógica de protocolo al SDK, elimina los tests host-side y vuelve
  imposible verificar `-Wpedantic` completo donde más importa (parsers).
- **Abstraer también FreeRTOS (colas, timers, tasks propias del core).**
  Descartado por ahora: duplicar primitivas del RTOS es costo sin beneficio
  mientras el único runtime sea ESP-IDF. El port expone solo lo que el core
  necesita, no una abstracción de OS completa.
- **Prometer los seis targets con Wi-Fi desde el día uno en CI.** Descartado
  para s2/c2: CI sin hardware real detrás promete validación que no existe.
  Dos tiers honestos > seis targets nominales.

## Consecuencias de seguridad

- El confinamiento del core permite auditar toda la lógica sensible sin leer
  código de plataforma, y habilita fuzzing host-side de parsers (requisito
  §4 de AGENTS.md) contra la misma lógica que corre en el dispositivo.
- La paridad de features de seguridad por target queda explícita arriba;
  agregar un target nuevo exige revisar esta lista, no asumirla.
- Riesgo: que la capa port se vuelva una fuga disfrazada (interfaces que
  calcan APIs de ESP-IDF 1:1, trasladando semántica del SDK al core).
  Mitigación: la interfaz del port se diseña desde las necesidades del core,
  no desde la oferta del SDK; cualquier ampliación de la interfaz se revisa
  con ese criterio.

## Consecuencias de estabilidad

- Las divergencias por target quedan confinadas al port → la superficie de
  bugs específicos-de-chip se reduce y la matriz de CI los caza temprano.
- Costo: una indirección extra en cada acceso a plataforma. Aceptado: es
  pequeño frente al beneficio de testeabilidad y reúso.
- El guard de includes en CI convierte las violaciones de arquitectura en
  build rojo, no en deuda silenciosa acumulada.
