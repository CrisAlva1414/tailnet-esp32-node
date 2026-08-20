# Política de vendoring

Ver AGENTS.md §6 para el detalle completo. Resumen operativo:

Ninguna dependencia entra aquí sin que su entrada en este archivo incluya:

1. **Origen exacto**: URL del repo y commit hash o tag específico (nunca
   `main`/`master` flotante).
2. **Razón de vendoring**: por qué se trae esta dependencia en vez de
   reimplementar, y por qué se vendoriza en vez de usar un gestor de paquetes
   de ESP-IDF (`idf.py add-dependency`) cuando ese camino esté disponible.
3. **Verificación hecha**: qué se revisó antes de traerla (historial del repo,
   CVEs conocidos, si está mantenida activamente, si es la implementación
   "de referencia" del algoritmo o una reimplementación de terceros).

Preferencia por defecto: **mbedTLS** (ya incluida en ESP-IDF) para primitivas
criptográficas estándar. Cualquier alternativa (ej. monocypher, una
implementación específica de Noise Protocol) requiere ADR propio justificando
por qué mbedTLS no alcanza para ese caso puntual, antes de agregarse aquí.

## Dependencias actuales

_Ninguna todavía. Esta tabla se completa a medida que se agregan._

| Dependencia | Origen (repo@commit) | Razón | ADR asociado |
|---|---|---|---|
| — | — | — | — |
