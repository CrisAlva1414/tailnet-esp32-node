# Análisis estático

AGENTS.md §4 exige análisis estático en cada sesión relevante. La fuente de
verdad de los flags es el workflow `.github/workflows/ci.yml` (job
`static-analysis`); este documento explica el alcance y las reglas de
decisión. **Mantener ambos en sync.**

## Herramienta y alcance

- `cppcheck` con `--enable=warning,portability --std=c11 --language=c
  --error-exitcode=1`.
- Se corre sobre `main/` y `components/tsnode/src/`, con include path de
  `components/tsnode/include`. `--suppress=missingIncludeSystem` porque
  cppcheck no resuelve los headers de ESP-IDF fuera del entorno de build; la
  compilación real (job `build`, ambos targets) es la que valida contra
  ESP-IDF completo.
- `--inline-suppr` permite supresiones inline `// cppcheck-suppress <id>`,
  pero por AGENTS.md §4 un hallazgo relacionado a memoria o crypto no se
  descarta sin justificación escrita: toda supresión inline debe llevar en la
  línea anterior un comentario que explique por qué es seguro, y referenciar
  el ADR si aplica.

## Reglas de decisión

- CI rojo = no se comitea encima; se arregla o se justifica por escrito.
- Cuando el código de protocolo entre (parsers ts2021/Noise/WireGuard), se
  evalúa sumar `--enable=all` sobre esos directorios y/o clang-tidy con
  config propia — decisión registrada aquí al momento.

## Estado

- Versión inicial: solo `main/main.c` existe; el valor real del job empieza
  con el primer módulo del componente.
