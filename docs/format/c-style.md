# Estilo de código C

Complementa las reglas no negociables de AGENTS.md §4. Esto es lo específico de
esta base de código; §4 son las reglas de seguridad/estabilidad que no cambian.

## Estándar y flags

- C11. Compilación con `-std=c11 -Wall -Wextra -Wpedantic -Werror`.
- `-Werror` se mantiene siempre activo en CI y en build local. No se comitea
  código que solo compila con warnings suprimidos.
- Formateo automático: `.clang-format` en la raíz del repo es la configuración
  canónica (indentación 4, límite 100 columnas). Este documento define las
  convenciones que el formateador no cubre.

## Naming

- Funciones y variables: `snake_case`.
- Constantes y macros: `UPPER_SNAKE_CASE`.
- Tipos (`struct`, `enum`, `typedef`): `PascalCase` con sufijo del dominio,
  ej. `TsNodeKey`, `TsFrameHeader`.
- Prefijo de módulo en funciones públicas del componente `tsnode`:
  `tsnode_<verbo>_<sujeto>`, ej. `tsnode_key_load()`, `tsnode_frame_parse()`.
- Nada de abreviaturas ambiguas fuera de las estándar del dominio (`buf`, `len`,
  `ctx` sí; abreviar `password` como `pwd` o similar, evaluar caso a caso).

## Organización de archivos

- Un `.h` por módulo lógico en `include/`, implementación correspondiente en
  `src/`. No headers "paraguas" que agrupen módulos no relacionados.
- Headers con guardas `#ifndef`/`#define` explícitas, no `#pragma once` (para
  mantener compatibilidad amplia con toolchains de ESP-IDF más antiguos si
  hiciera falta bajar de versión).
- Nada de lógica de negocio en headers salvo `static inline` genuinamente
  triviales (getters simples). Todo lo demás va a `.c`.

## Manejo de errores

- Tipo de retorno de error propio del proyecto (definir en
  `components/tsnode/include/tsnode_err.h` cuando se cree), no mezclar
  convenciones de retorno de distintas bibliotecas de terceros sin normalizar
  en la frontera del componente.
- Todo error se puede loguear con contexto suficiente para depurar sin
  necesitar reproducir localmente — pero nunca con material sensible (ver
  AGENTS.md §2.1, último punto).

## Comentarios

- Comentarios explican *por qué*, no *qué* (el código ya dice qué). Si una
  decisión de timing, tamaño de buffer, o validación de protocolo tiene una
  razón no obvia, el comentario referencia el ADR correspondiente
  (`// ver docs/adr/000X-....md`) en vez de reexplicar el razonamiento completo
  inline.
