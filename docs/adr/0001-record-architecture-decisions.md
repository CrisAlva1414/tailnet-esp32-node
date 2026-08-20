# ADR-0001: Registrar las decisiones de arquitectura como ADRs

- Estado: aceptado
- Fecha: 2026-08-20

## Contexto

Este proyecto reimplementa, de forma selectiva y desde cero en C, el enfoque de
[`alfs/tailscale-iot`](https://github.com/alfs/tailscale-iot) para llevar un
cliente Tailscale mínimo a un ESP32. El proyecto de referencia es explícito en
llamarse a sí mismo un "Frankenstein PoC": funciona, pero incluso su autor no
recomienda tocarlo a mano ni tomarlo como base para producción, y documenta
limitaciones conocidas (sin DERP, sin IPv6, resets de watchdog por timing de
crypto mal calibrado).

Este repo existe para no repetir ese resultado. Eso requiere que cada decisión
de protocolo, memoria y criptografía quede registrada con su razonamiento y,
sobre todo, con sus consecuencias de seguridad explícitas — no implícitas en un
commit message o en la memoria de quien lo escribió.

## Decisión

Toda decisión de arquitectura, de manejo de claves, o de superficie de ataque se
registra como un ADR en `docs/adr/`, siguiendo la plantilla `0000-template.md`.
Un ADR se crea **antes** de escribir el código que la implementa, no después
como documentación retroactiva. opencode, al operar en este repo, se detiene y
solicita o redacta el ADR correspondiente antes de tocar código cuando detecta
que la tarea pedida implica una decisión de este tipo (ver AGENTS.md §5).

## Alternativas consideradas

- **No documentar decisiones de arquitectura, confiar en commits + código
  comentado.** Descartado: es exactamente el patrón que produjo el estado actual
  del proyecto de referencia — funciona, pero nadie (ni el propio autor) puede
  explicar con confianza por qué cada parámetro tiene el valor que tiene.
- **Un único documento de arquitectura vivo (`ARCHITECTURE.md`) en vez de ADRs
  numerados.** Descartado: pierde el historial de *por qué* se cambió algo;
  un documento vivo tiende a sobreescribir el contexto de decisiones pasadas.

## Consecuencias de seguridad

Ninguna decisión de seguridad queda sin justificación escrita y sin registro de
qué alternativas se descartaron. Esto no reduce superficie de ataque por sí
mismo, pero reduce drásticamente el riesgo de que una decisión insegura se tome
por default o por prisa sin que quede rastro de que se tomó.

## Consecuencias de estabilidad

Ninguna directa. Indirectamente, forzar el registro de decisiones de timing y
manejo de recursos (ej. intervalos de crypto, tamaños de buffer) antes de
implementarlas reduce la probabilidad de repetir el patrón de "ajustar el
número hasta que deje de resetear" que el proyecto de referencia documenta
haber sufrido.
