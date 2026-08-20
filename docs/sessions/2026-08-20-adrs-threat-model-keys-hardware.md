# 2026-08-20 — cierre de ADRs 0002–0004 (modelo de amenaza, claves, hardware)

## Contexto

Segunda sesión del proyecto, con prompt de arranque explícito del usuario.
Objetivo: completar ADR-0002 y ADR-0003, crear ADR-0004 (target de hardware y
nombre de componente), y dejar el proyecto en condiciones de que la próxima
sesión pueda escribir `main.c` real — sin escribir todavía ningún código de
protocolo, según AGENTS.md §5.1.

## Cambios

- `docs/adr/0002-threat-model.md` — completado: activos en reposo enumerados
  (node key, machine key, auth key transitoria, PSK Wi-Fi), supuestos de
  capacidad del atacante físico (dump SPI sin desoldar como caso base;
  glitching/side-channel fuera de alcance v1) y remoto (LAN hostil en
  operación normal; sin MITM contra control plane — confianza en PKI pública
  con validación TLS estricta, sin pinning en v1), política fail-closed de
  tiempo para TLS, y tabla explícita de lo fuera de alcance v1. Estado:
  `propuesto`, esperando confirmación del usuario.
- `docs/adr/0003-key-storage-strategy.md` — completadas las partes abiertas:
  NVS encryption con esquema HMAC-based (`CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC`,
  clave en eFuse, sin partición `nvs_keys`), flash encryption Release con
  procedimiento e irreversibilidad documentados, Secure Boot v2 pospuesto con
  cuatro disparadores explícitos de activación, separación build dev/prod con
  JTAG deshabilitado en producción vía hardening del primer boot, ciclo de
  vida de la auth key post-consumo (purga NVS + zeroize). Todos los símbolos
  de sdkconfig verificados contra docs.espressif.com (C3, latest) en esta
  sesión. Estado: `propuesto`, esperando confirmación del usuario.
- `docs/adr/0004-hardware-target-and-component-naming.md` — creado: ESP32-C3
  confirmado como target con restricción de revisión ≥ v0.3 (ECO3); nombre
  `tsnode` confirmado, deja de ser provisional. Estado: `propuesto`,
  esperando confirmación del usuario.

Ningún archivo tocado en `main/` ni `components/tsnode/src/`, según las
reglas de la sesión.

## Decisiones de seguridad tomadas o revisadas

Todas quedaron registradas en los ADRs correspondientes y están pendientes de
confirmación para pasar a `aceptado`. Resumen de lo propuesto:

1. Corte físico: dump SPI sin desoldar = caso base; glitching y side-channel
   sobre eFuses = fuera de alcance v1 (ADR-0002).
2. Atacante remoto = miembro hostil de la LAN compartida en operación normal;
   NO se asume MITM contra Tailscale SaaS; ancla = PKI pública + validación
   TLS estricta; sin cert pinning en v1 (ADR-0002).
3. NVS encryption por esquema HMAC (raíz de confianza independiente de la
   clave de flash encryption), no por esquema flash-enc-based (ADR-0003).
4. Flash encryption obligatoria en modo Release antes de salir del banco de
   pruebas; Development solo dentro del banco (ADR-0003).
5. Secure Boot v2 pospuesto, no bloqueante v1, con disparadores explícitos
   (salida del banco, despliegue desatendido, OTA previo, indicios de
   tampering) (ADR-0003).
6. Builds dev/prod separados en compile-time; JTAG deshabilitado en
   producción; `CONFIG_SECURE_BOOT_ALLOW_JTAG` prohibida en prod (ADR-0003).

## Pendiente / bloqueado

- **Bloqueado en confirmación del usuario** (los tres ADRs quedan en
  `propuesto` hasta que confirme en el chat): corte físico, supuesto MITM,
  esquema HMAC de NVS, SBv2 pospuesto, target C3 rev ≥ v0.3, nombre `tsnode`.
- Después de la confirmación: pasar ADRs a `aceptado`, crear
  `sdkconfig.defaults` (+ variantes dev/prod) con los flags ya documentados en
  ADR-0003, fijar versión de ESP-IDF pin, y actualizar README (sección Build)
  para reflejar el target confirmado.
- Recién entonces: primer código real (`main.c` mínimo que levanta Wi-Fi y
  logea estado, sin Noise/WireGuard), que además necesitará el ADR de
  arquitectura de protocolo antes de tocar ts2021.
