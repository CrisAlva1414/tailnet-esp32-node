# ADR-0003: Estrategia de almacenamiento de claves en flash

- Estado: propuesto
- Fecha: 2026-08-20

## Contexto

El ESP32 target no cuenta con elemento seguro dedicado en la variante base
(evaluar aparte, en un ADR posterior, si se adopta una variante con ATECC608
externo). Toda clave persistida en flash es en principio extraíble por alguien
con acceso físico y las herramientas correctas, salvo que se usen las
protecciones nativas del SoC. Ver AGENTS.md §2.1.

## Decisión (a completar en la primera sesión de implementación)

Pendiente de fijar con precisión:

- Activación de **NVS encryption** de ESP-IDF para el namespace donde se
  guarda la node key y cualquier credencial persistente.
- Activación de **flash encryption** en modo Release antes de cualquier
  despliegue fuera del banco de pruebas controlado del usuario. Documentar
  aquí el procedimiento exacto (`idf.py`, eFuses involucrados, e
  irreversibilidad del proceso una vez quemados los eFuses de producción).
- Alcance de **Secure Boot v2** para v1: bloqueante o pospuesto, con criterio
  explícito de qué evento dispara su activación (ej. el dispositivo deja el
  banco de pruebas).
- Estado de eFuses de debug/JTAG en builds de producción (deshabilitados) vs.
  builds de desarrollo (habilitados y marcados como tales, nunca la misma
  imagen para ambos casos).
- Manejo de la auth key de un solo uso durante el provisioning inicial: cómo
  llega al dispositivo, y confirmación explícita de que no queda persistida
  en flash una vez consumida y reemplazada por la node key propia.

## Alternativas consideradas

Pendiente — se completa junto con la decisión. Como mínimo debe compararse
NVS encryption + flash encryption nativos vs. cualquier esquema de cifrado de
aplicación hecho a mano (este último se descarta salvo justificación muy
fuerte, ya que reinventar cifrado a nivel de aplicación sobre un SoC que ya
ofrece esto nativamente es exactamente el tipo de decisión que este proyecto
busca evitar).

## Consecuencias de seguridad

Directas y centrales — este ADR define el mecanismo principal de mitigación
para el eje de amenaza física completo (AGENTS.md §2.1).

## Consecuencias de estabilidad

Flash encryption y Secure Boot v2 tienen implicancias operativas (ej.
dificultad de reflashear en desarrollo, irreversibilidad de eFuses quemados)
que deben quedar documentadas aquí para no sorprender en campo, especialmente
dado que el usuario ya resolvió un boot failure de Orange Pi por SPI bootloader
en otro proyecto — el mismo tipo de recuperación es sustancialmente más
limitado o imposible en un ESP32 con Secure Boot activo si algo sale mal.
