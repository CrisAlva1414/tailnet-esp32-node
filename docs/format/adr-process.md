# Proceso de ADR

## Numeración

- Secuencial, cuatro dígitos, sin huecos: `0001`, `0002`, ...
- El número se reserva creando el archivo (aunque quede como `propuesto`) para
  evitar colisiones si hay trabajo en paralelo.

## Estados

- `propuesto`: la decisión está redactada pero no se ha empezado a implementar,
  o está en discusión.
- `aceptado`: la decisión rige el código actual del repo.
- `rechazado`: se consideró y se descartó explícitamente. Se conserva (no se
  borra) porque documenta una alternativa ya evaluada, para no reabrir la
  discusión sin nueva información.
- `superseded por ADR-YYYY`: una decisión posterior reemplazó a esta. El ADR
  viejo se mantiene con este estado y un link al nuevo; no se edita su
  contenido original salvo para agregar la nota de reemplazo.

## Quién puede proponer uno

Cualquiera — el usuario o opencode. opencode debe proponer un ADR (redactar el
archivo en estado `propuesto` y señalarlo explícitamente en su respuesta, no
solo crearlo silenciosamente) cuando, en el curso de una tarea pedida, detecta
que se está por tomar una decisión que:

- Cambia el modelo de amenaza (AGENTS.md §2) de cualquier forma.
- Introduce una dependencia nueva de terceros (ver AGENTS.md §6).
- Cambia cómo se almacenan, transmiten, o comparan claves/tokens/secretos.
- Cambia el alcance funcional descrito en AGENTS.md §1 (ej. agregar DERP,
  IPv6, subnet routing).

En esos casos opencode no continúa escribiendo código hasta que el ADR exista
al menos en estado `propuesto` y el usuario lo haya visto.

## Revisión

Un ADR pasa de `propuesto` a `aceptado` solo con confirmación explícita del
usuario en la conversación, nunca automáticamente por el solo hecho de haber
sido redactado.
