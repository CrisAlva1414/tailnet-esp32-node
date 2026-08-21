# 2026-08-20 — revisión por primer dispositivo real, ADR-0005, CI y publicación

## Contexto

Tercera pasada del día. El usuario informó el primer dispositivo a desplegar
(M5Stack Core 2 como intercomunicador), confirmó la estrategia de reúso como
librería para otros proyectos, pidió publicar el repo como público con
licencia permisiva, y agregar integraciones que aceleren el desarrollo.

## Cambios

- `docs/adr/0003-key-storage-strategy.md` — revisado: esquema NVS unificado
  flash-enc-based (el HMAC-based requiere periférico inexistente en ESP32
  clásico); matizaciones por target de flash encryption (algoritmo y eFuses
  difieren; detalles del ESP32 clásico a verificar contra su doc específica);
  nota SBv2 disponible en ambos targets (ambos ECO3+).
- `docs/adr/0004-hardware-target-and-component-naming.md` — reescrito:
  multi-target con validación primaria en M5Stack Core 2 (ESP32-D0WDQ6-V3,
  verificado contra docs.m5stack.com), C3 secundario; implicancias declaradas
  (puente CP2104/CH9102F, JTAG no expuesto en la placa, dual core); alcance
  nuevo registrado (intercomunicador = dominio de aplicación separado del
  componente, con su propio ADR futuro).
- `docs/adr/0005-packaging-and-reuse.md` — creado: `tsnode` como unidad de
  reúso (componente ESP-IDF autocontenido), frontera estricta include/src,
  integración host-side vía tailnet (no vía librería), canales de distribución
  por etapa (copia → submodule → Component Registry), publicación pública
  aceptada sin seguridad por oscuridad.
- `.github/workflows/ci.yml` — CI: build matrix esp32 + esp32c3 (ESP-IDF
  v5.5) y job cppcheck.
- `docs/format/static-analysis.md` — config versionada del análisis estático
  (exigida por AGENTS.md §4).
- `.clang-format` + puntero en `docs/format/c-style.md`.
- `README.md` — targets actualizados (Core 2 primario, C3 secundario),
  referencia a ADRs 0004/0005.
- Debug de CI (runs 32434301795 → 32435176542): los builds fallaban porque
  `-std=c11 -Wpedantic -Werror` choca contra headers de ESP-IDF/newlib.
  Tres iteraciones: gnu11 no alcanza (el pedwarn de `#include_next` lo emite
  el preprocesador y no depende del modo ISO); `#pragma GCC diagnostic`
  scoped tampoco lo suprime (verificado en runs 32434703866 y 32434949932).
  Resolución final: firmware con `-std=gnu11 -Wall -Wextra -Werror`, sin
  `-Wpedantic`; compensación documentada — parsers de protocolo se compilarán
  en host (`tests/unit/`) con pedantic completo. Convención fijada en
  `docs/format/c-style.md`. CI verde en run 32435176542 (cppcheck + esp32 +
  esp32c3).

## Decisiones de seguridad tomadas o revisadas

- Unificación del esquema NVS en flash-enc-based: trade-off declarado de raíz
  de confianza única, aceptado porque extraer la clave eFuse exige ataque
  físico fuera de alcance v1 (ADR-0002) y la uniformidad reduce errores de
  implementación. Queda en ADR-0003 revisado.
- Publicación del repo público: escaneo de historial completo en busca de
  secretos (auth keys, PSKs, tokens) — limpio. La exposición del código se
  asume explícitamente como no-degradante (sin seguridad por oscuridad),
  registrada en ADR-0005.
- El caso intercomunicador eleva el impacto de compromiso de nodo (micrófono
  en espacio físico); registrado en ADR-0004 como requisito del futuro ADR de
  aplicación de audio.

## Pendiente / bloqueado

- Confirmación única del usuario sobre los cinco ADRs (0002–0005), todos en
  `propuesto`. Tras confirmación: pasarlos a `aceptado`, crear
  `sdkconfig.defaults` (+ dev/prod), fijar pin de ESP-IDF definitivo.
- **Nueva desviación de AGENTS.md §4 pendiente de la misma confirmación**:
  firmware compila con `-std=gnu11 -Wall -Wextra -Werror` (sin `-Wpedantic`,
  incompatible con headers de ESP-IDF/newlib; evidencia y compensación en
  `docs/format/c-style.md`). §4 requiere actualización formal cuando el
  usuario confirme.
- Anotación no bloqueante en CI: `actions/checkout@v4` corre forzado sobre
  Node 24 (deprecation de Node 20); migrar a checkout@v5+ cuando exista.
- Próxima sesión de código: `main.c` mínimo Wi-Fi + logs, previo ADR de
  arquitectura de protocolo antes de tocar ts2021.
