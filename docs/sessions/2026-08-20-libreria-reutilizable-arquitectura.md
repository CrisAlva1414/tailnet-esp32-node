# 2026-08-20 — librería reutilizable: arquitectura en capas y familia de targets

## Contexto

El usuario expandió el alcance del proyecto: el M5Stack Core 2 (intercom)
es solo el primer dispositivo; la meta es una **librería reutilizable** para
"todo tipo de ESP32" en domótica y automatización. Pidió iterar hasta tener
la biblioteca reutilizable concreta.

## Cambios

- `docs/adr/0006-layered-architecture-and-target-family.md` — creado: capas
  core/port/app (core en C puro sin headers de plataforma, verificado por
  guard en CI), familia de targets en dos tiers (Tier 1 CI obligatorio:
  esp32/c3/s3/c6; Tier 2: s2/c2; excluido h2 por falta de Wi-Fi), paridad de
  seguridad por target y layout futuro multi-app (propuesto, requiere
  confirmación).
- `docs/adr/0004` — ampliado con nota de alcance: la tabla de targets queda
  subsumida por los tiers de ADR-0006; conserva el rol de fijar el Core 2
  como validación primaria v1.
- `components/tsnode/` — esqueleto real de la librería:
  - `include/tsnode.h`, `tsnode_err.h`, `tsnode_config.h`: API pública
    (ciclo de vida, códigos de error, versión). Sin fugas de plataforma.
  - `src/tsnode.c`: máquina de estados mínima; `tsnode_start()` retorna
    NOT_IMPLEMENTED deliberadamente hasta ADR-0002/0003 aceptados.
  - `src/port/tsnode_port.h` + `src/port/esp_idf/`: interfaz port
    (entropía, tiempo monotónico) con stubs ESP-IDF.
  - `Kconfig` con `TSNODE_LOG_LEVEL`.
- `main/main.c` — app de referencia ahora consume la API pública
  (`tsnode_init/state_get/start`), probando el enlace real componente↔app.
- `.github/workflows/ci.yml` — matriz ampliada a los 4 targets Tier 1 +
  paso "Guard architecture": falla si el core incluye headers de plataforma
  (regla dura de ADR-0006 automatizada).
- `README.md` — historia de reúso y tabla de targets actualizada.

## Decisiones de seguridad tomadas o revisadas

- Paridad de seguridad en toda la familia Tier 1 declarada en ADR-0006:
  flash encryption + NVS encryption flash-enc-based (ADR-0003) disponibles
  en todos; SBv2 disponible en todos (clásico exige ECO3+). Ninguna
  variante con claves menos protegidas que otra.
- Piso de RAM fijado en C3 (~400 KB): ningún módulo puede asumir PSRAM;
  headroom bajo carga crypto es requisito para declarar soporte real.
- El confinamiento del core habilita fuzzing host-side futuro de parsers
  contra la misma lógica que corre en el dispositivo (requisito §4).
- Riesgo registrado: que el port se vuelva fuga disfrazada calando APIs del
  SDK; mitigación documentada (interfaz diseñada desde necesidades del core).

## Pendiente / bloqueado

- Confirmación única del usuario sobre ADRs 0002–0006 (todos `propuesto`) +
  desviación §4 (gnu11 sin pedantic en firmware) + futuro layout `apps/`
  multi-app (§3 de AGENTS.md).
- Iteración de CI necesaria: 1 ciclo de fix (includes privados del port +
  `<stddef.h>`); run final 32436079865 verde en los 5 jobs.
- Próxima sesión de código: ADR de arquitectura de protocolo antes de tocar
  ts2021; luego implementación real del port (entropía vía esp_fill_random,
  uptime vía esp_timer).

## Nota

El usuario no respondió aún la confirmación formal de los ADRs pedida al
cerrar la sesión anterior; este avance se apoyó en su instrucción explícita
de "iterar hasta biblioteca reutilizable". Los ADRs nuevos siguen en
`propuesto` — nada quedó marcado como aceptado sin confirmación.
