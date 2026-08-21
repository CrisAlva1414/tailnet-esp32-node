# tailnet-esp32-node

Cliente Tailscale mínimo en C puro para ESP32 (ESP-IDF, sin ESPHome), construido
como reimplementación selectiva — no fork, no submódulo — del enfoque de
[`alfs/tailscale-iot`](https://github.com/alfs/tailscale-iot).

**Estado: inicialización.** No hay todavía código de protocolo. Ver
`docs/sessions/` para el registro de avance y `docs/adr/` para las decisiones
de arquitectura y seguridad, tomadas y pendientes.

## Qué es esto

Llevar dispositivos ESP32 a una tailnet personal de Tailscale (SaaS, no
Headscale), con IP 100.x.x.x y conectividad directa vía WireGuard sobre NAT
traversal, priorizando por sobre todo: **seguridad y estabilidad**.

Targets (ver `docs/adr/0004-hardware-target-and-component-naming.md`):
validación primaria v1 sobre **M5Stack Core 2** (ESP32 clásico, primer
despliegue real: intercomunicador); ESP32-C3 soportado como target
secundario. El componente `tsnode` es reutilizable como componente ESP-IDF en
otros proyectos (`docs/adr/0005-packaging-and-reuse.md`).

## Qué NO es esto (v1)

- No soporta DERP (relay): si no hay ruta UDP directa, el nodo no conecta.
- No soporta IPv6, subnet routing, exit node, ni MagicDNS local.
- No es un fork de `alfs/tailscale-iot` ni depende de su código; es una
  reimplementación propia inspirada en su enfoque general y en las
  limitaciones que ese proyecto ya documentó (ver AGENTS.md para el detalle).

## Por qué existe, dado que ya existe `alfs/tailscale-iot`

Ese proyecto es un proof-of-concept honesto sobre sus propios límites: su
autor lo describe como "Frankenstein PoC", código que funciona pero que
desaconseja tocar a mano o usar como base de producción. Este repo parte de
ahí pero con el objetivo inverso: cada decisión de protocolo, memoria y
manejo de claves queda respaldada por un ADR (`docs/adr/`), con modelo de
amenaza explícito y con seguridad física y remota tratadas con igual peso.

## Leer antes de contribuir o generar código aquí

`AGENTS.md` es el documento operativo completo (alcance, modelo de amenaza,
reglas de C, estructura, proceso). Cualquier agente (humano o LLM) que trabaje
en este repo debe leerlo primero.

## Build

Requiere ESP-IDF instalado (ver documentación oficial de Espressif para la
versión soportada — se fijará en `sdkconfig.defaults` y en un ADR cuando el
target de hardware quede confirmado).

```
idf.py set-target esp32     # primario v1 (M5Stack Core 2)
idf.py set-target esp32c3   # secundario
idf.py build
```

Sin flash encryption activa, esto es solo para desarrollo local en el banco de
pruebas del usuario. Ver `docs/adr/0003-key-storage-strategy.md` antes de
flashear cualquier build fuera de ese contexto.

## Licencia

Ver `LICENSE`.
