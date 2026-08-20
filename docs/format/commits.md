# Convención de commits

Conventional Commits, con un tipo adicional propio del proyecto para cambios
de seguridad que merecen visibilidad especial en el historial.

## Formato

```
<tipo>(<scope opcional>): <resumen en imperativo, minúsculas, sin punto final>

<cuerpo opcional: por qué, no qué>

<footer opcional: referencias a ADR/sesión>
```

## Tipos

- `feat`: funcionalidad nueva.
- `fix`: corrección de bug (no de seguridad — ver `sec` abajo).
- `sec`: cambio cuyo motivo primario es seguridad (endurecer validación,
  cerrar una superficie, corregir manejo de claves). Se usa `sec` en vez de
  `fix` incluso si técnicamente corrige un bug, para que el historial permita
  filtrar `git log --grep '^sec'` y auditar todos los cambios de seguridad.
- `docs`: solo documentación (incluye ADRs y sesiones).
- `test`: solo tests.
- `build`: sistema de build, CMake, sdkconfig.
- `refactor`: cambio de estructura sin cambio de comportamiento observable.
- `chore`: mantenimiento sin impacto en código de producto.

## Reglas

- Un commit de tipo `sec` referencia siempre el ADR relevante en el footer
  (`Refs: docs/adr/0003-key-storage-strategy.md`) cuando exista.
- No se mezclan cambios de tipo `sec` con `feat`/`refactor` en el mismo commit,
  aunque toquen el mismo archivo — se separan para que el historial de
  seguridad quede limpio y auditable de forma independiente.
- El footer puede referenciar la sesión de trabajo correspondiente:
  `Session: docs/sessions/2026-08-20-slug.md`.
