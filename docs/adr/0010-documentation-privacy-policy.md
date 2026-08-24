# ADR-0010: política de privacidad documental (separación público/privado)

- Estado: aceptado
- Fecha: 2026-08-24

## Contexto

Este repositorio es **público** y se posiciona como librería reutilizable para
conectar ESP32 a Tailscale. Todo `.md` versionado y todo comentario de código es
visible por cualquiera. Durante una auditoría (2026-08-24) se encontró que la
documentación de sesiones había mezclado contenido técnico valioso con datos
personales del operador: SSID y PSK reales del Wi-Fi doméstico, IPs de tailnet,
IPs LAN, y detalles del entorno físico. El PSK estaba en la historia de git
pusheada a GitHub.

El problema de fondo no fue un commit aislado sino la ausencia de una regla que
diga, **antes de escribir**, qué es público y qué no. La documentación de este
proyecto cumple dos funciones distintas con requisitos distintos:

1. **Función pública**: documentar la librería, sus decisiones de arquitectura
   y seguridad, y los hallazgos técnicos de protocolo (Noise/ts2021/HTTP2) —
   contenido genuinamente útil para terceros.
2. **Función privada**: registrar operaciones del despliegue personal del
   autor (credenciales de banco de pruebas, IPs asignadas, runbooks de su
   tailnet) — necesario para trabajar, inaceptable publicar.

## Decisión

1. **Toda la documentación versionada nace pública por defecto** y debe poder
   leerla un tercero sin aprender nada sobre la infraestructura personal del
   operador.
2. Se crea `docs/private/` (**gitignoreada, jamás versionada**) para material
   operativo personal: runbooks del despliegue propio, credenciales de entorno,
   notas con IPs/SSIDs reales.
3. Las sesiones técnicas (`docs/sessions/`) siguen públicas **sanitizadas**:
   los datos personales se reemplazan por placeholders (`<ssid>`, `<psk>`,
   `<ip-tailnet>`, `<ip-lan>`). El detalle sin sanitizar vive en `docs/private/`
   si hace falta.
4. Los ADRs citan valores de protocolo reales (públicos por definición: son del
   protocolo de Tailscale) pero nunca valores de identidad del despliegue del
   autor (IPs asignadas a SUS nodos, nombres de SU red).
5. El código fuente no contiene endpoints ni claves literales de ningún entorno,
   ni siquiera de prueba; los parámetros de test ingresan por consola/Kconfig.
6. La convención operativa completa está en
   [`docs/format/documentation-privacy.md`](../format/documentation-privacy.md).
7. El incidente ya ocurrido se remedia en dos capas: rotación inmediata de la
   credencial expuesta (obligatoria, la exposición ya pasó) y scrub de la
   historia con `git-filter-repo` (defensa en profundidad para quien lea el
   repo a futuro).

## Alternativas consideradas

- **Mover todas las sesiones a privado**: descartado — pierde el valor público
  real del registro técnico (los hallazgos de protocolo son de lo más útil del
  repo para terceros) y contradice la disciplina documental del proyecto.
- **Repo privado / recrearlo desde cero**: descartado — el objetivo declarado
  es una librería pública; recrear pierde historia de decisiones sin eliminar
  el riesgo residual (forks/clones previos).
- **No reescribir historia, solo rotar credenciales**: insuficiente — aunque la
  PSK rote, el historial seguiría exponiendo topología (número de unidad,
  esquema de nombres, IPs). El repo es joven (~15 commits); el costo del scrub
  es mínimo comparado con la exposición permanente.

## Consecuencias de seguridad

- Reduce la superficie de reconocimiento contra el operador: un atacante que
  lea el repo deja de obtener SSID/PSK, número de departamento, IPs de tailnet
  y topología LAN.
- No elimina exposiciones pasadas: cualquier clon/fork previo al scrub conserva
  los datos → la rotación de credenciales es obligatoria e independiente de
  este ADR.
- Introduce un nuevo riesgo de proceso: que alguien escriba datos privados en
  un archivo público por hábito. Mitigación: checklist pre-commit en
  `docs/format/documentation-privacy.md` + regla en AGENTS.md §7 + grep de
  verificación antes de pushear.
- Sin efecto sobre el firmware ni sobre el modelo de amenaza del dispositivo
  (§2): esta política gobierna el repositorio, no el producto.

## Consecuencias de estabilidad

- Ninguna sobre el firmware: los cambios de código son remover constantes de
  test hardcoded (parámetros pasan a entrar por consola).
- Riesgo menor de documentación: referencias internas pueden apuntar a archivos
  que en un clone fresco no existen (`docs/private/`). Se mitiga marcando esas
  referencias explícitamente como "local, no versionado".
