# ADR-0002: Modelo de amenaza aceptado para v1

- Estado: propuesto
- Fecha: 2026-08-20

## Contexto

AGENTS.md §2 fija los lineamientos generales de modelo de amenaza para este
proyecto: amenaza física (dispositivo con acceso físico, extracción de flash) y
amenaza remota (protocolo, red, MITM, replay) se tratan con **igual prioridad**,
sin que una se optimice a costa de la otra sin justificación explícita.

Este ADR existe para formalizar esas líneas generales en decisiones concretas
antes de escribir el primer módulo que toque red o almacenamiento persistente.
Se marca como "propuesto" deliberadamente: debe completarse y pasar a "aceptado"
en la primera sesión de trabajo real sobre el componente `tsnode`, no quedarse
como placeholder.

## Decisión (a completar en la primera sesión de implementación)

Pendiente de definir con precisión, como mínimo:

- Qué activos concretos se protegen en reposo (node key, PSK Wi-Fi, cualquier
  token de registro) y con qué mecanismo exacto de ESP-IDF (flash encryption +
  NVS encryption, ver ADR-0003 para el detalle de almacenamiento).
- Qué se asume sobre el atacante físico: ¿se asume capacidad de dump de flash
  SPI sin desoldar? ¿Se asume glitching de voltaje? Esto determina si Secure
  Boot v2 es bloqueante para v1 o queda para una fase posterior con su propio ADR.
- Qué se asume sobre el atacante remoto: ¿se asume que puede estar en la misma
  red Wi-Fi local del ESP32 (LAN compartida)? ¿Se asume que puede intentar
  MITM antes del handshake Noise? El diseño de provisioning inicial (§2.3 de
  AGENTS.md) depende directamente de esta respuesta.
- Qué se considera fuera de alcance para v1 explícitamente (ej. ataques de
  cadena de suministro sobre el propio ESP32, ataques a la infraestructura de
  Tailscale SaaS) y por qué.

## Alternativas consideradas

Pendiente — se completa junto con la decisión.

## Consecuencias de seguridad

Este ADR *es* la formalización del eje de seguridad del proyecto; no aplica
completarlo como si fuera un ADR más entre otros. Debe revisarse cada vez que
se agregue una superficie nueva (ej. si en el futuro se habilita DERP o
provisioning por Wi-Fi AP).

## Consecuencias de estabilidad

Ninguna directa, pero el modelo de amenaza remoto acordado aquí condiciona
directamente los límites de validación de protocolo del componente `tsnode`
(ver AGENTS.md §4), que sí tienen impacto de estabilidad.
