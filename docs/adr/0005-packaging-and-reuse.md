# ADR-0005: Estrategia de empaquetado y reúso del componente `tsnode`

- Estado: propuesto (esperando confirmación explícita del usuario para pasar a `aceptado`)
- Fecha: 2026-08-20

## Contexto

El usuario confirmó que necesita reutilizar este trabajo en otros proyectos:
nuevos dispositivos embebidos por un lado, y herramientas de análisis
host-side por otro. El repo se publica como público con licencia MIT. La
pregunta de arquitectura es qué es exactamente lo reutilizable, por qué canal
se distribuye, y qué garantiza la frontera del componente — decidido antes de
escribir el primer `.c` de protocolo, porque condiciona cómo se dibujan los
módulos desde el día uno.

## Decisión

### Qué es reutilizable y qué no

- **`components/tsnode/` es la unidad de reúso**: un componente ESP-IDF
  autocontenido que implementa el cliente Tailscale (transporte, identidad,
  cifrado). No contiene lógica de aplicación.
- **La capa de aplicación vive fuera** (`main/` en este repo; en proyectos
  consumidores, su propio código de app): audio/UI del intercomunicador,
  sensores, lo que cada proyecto necesite. Ningún archivo de aplicación dentro
  del componente, nunca.
- **La integración host-side NO es vía librería**: las herramientas de
  análisis del usuario (Python/scripts/NAS) consumen los dispositivos **vía
  tailnet** — cada dispositivo expone su servicio en su IP 100.x.y.z y las
  herramientas se conectan por red con la autenticación que la propia tailnet
  impone. `tsnode` depende de ESP-IDF (NVS, Wi-Fi, FreeRTOS, mbedTLS) y no
  tiene sentido compilarlo fuera de ese entorno.

### Frontera del componente (reglas de disciplina desde el primer archivo)

1. API pública exclusivamente en `components/tsnode/include/`; todo lo demás
   es interno (`src/`) y puede cambiar sin aviso.
2. El componente no conoce `main/`: ninguna referencia inversa, ningún header
   de aplicación incluido desde el componente.
3. Debe compilar standalone: `idf.py build` de un proyecto mínimo que solo
   dependa de `tsnode` es el test canónico de que la frontera no se rompió
   (CI lo verifica compilando este mismo repo, donde `main/` es deliberadamente
   mínimo).
4. Las dependencias de terceros del componente se declaran en su
   `CMakeLists.txt` (y en `third_party/README.md` cuando se vendoricen), nunca
   asumidas presentes en el proyecto consumidor.

### Canales de distribución, por etapa

| Etapa | Canal | Cuándo |
|---|---|---|
| Ahora | Copia vendorizada del componente entre repos propios | Hasta que la API se estabilice |
| Cerca | Git submodule apuntando a tag fijo | Cuando exista el primer proyecto consumidor real |
| Después | Managed component del ESP Component Registry (`idf.py add-dependency`) | Solo cuando la API pública esté estable y versionada |

Versionado por git tags semver-ish (`v0.x` mientras la API sea inestable;
`v1.0.0` recién con API pública congelada y test vectors pasando). El paso al
Component Registry exige además un `idf_component.yml` y revisión de que nada
interno se filtre al paquete — decisión que se toma en su momento, no ahora.

### Publicación del repo como público

El repo se publica en GitHub como público bajo licencia MIT (ya presente).
Consecuencia aceptada explícitamente: la implementación del protocolo es
auditable por cualquiera, incluido un eventual atacante. Esto es consistente
con el diseño del proyecto — la seguridad descansa en el modelo de amenaza de
ADR-0002 y en las claves, no en ocultar el código (no hay seguridad por
oscuridad que perder). Condición dura verificada antes de publicar: ningún
secreto real en el historial (auth keys, PSKs, tokens) — escaneo hecho en la
sesión que publica.

## Alternativas consideradas

- **Convertir `tsnode` en librería C portable (POSIX) para linkearla también
  host-side.** Descartado: duplicaría las abstracciones de plataforma (NVS,
  sockets, timers de FreeRTOS) solo para satisfacer un caso de uso que la
  tailnet ya resuelve por red. Costo alto, beneficio nulo: las herramientas
  host-side hablan WireGuard/Tailscale nativo o HTTP sobre la tailnet, no C.
- **Monorepo de aplicaciones (este repo conteniendo todos los futuros
  dispositivos).** Descartado: acopla ciclos de vida de proyectos distintos y
  convierte cada dispositivo nuevo en un commit contra el repo del cliente de
  protocolo. Cada proyecto de dispositivo consume `tsnode`; este repo queda
  como desarrollo del componente + app de referencia mínima.
- **Publicar directo al Component Registry desde el día uno.** Descartado:
  publicar una API inestable genera consumidores rotos en cada cambio; el
  registro es para cuando la frontera esté probada en al menos un consumo
  real.

## Consecuencias de seguridad

- Repo público = el código de parsing de protocolo es estudiabile por un
  adversario antes de encontrarse con el dispositivo. Aceptado y deseado: es
  la misma exposición que tienen Tailscale y WireGuard (open source), y fuerza
  a que la validez de las decisiones repose en los ADRs, no en el secreto.
- La frontera estricta del componente reduce el riesgo de que código de
  aplicación (donde viven necesidades "prácticas" tipo logs verbosos o atajos)
  contamine el camino crítico de claves/crypto, que queda confinado y
  auditable en `tsnode`.
- Riesgo nuevo a vigilar: que comodidad de reúso presione a relajar validaciones
  "para que compile en otro proyecto". Regla: cualquier relajación de una
  verificación de seguridad del componente para facilitar reúso requiere ADR,
  igual que cualquier otra decisión de seguridad.

## Consecuencias de estabilidad

- Copias vendorizadas tempranas divergen del upstream sin mecanismo de detección
  — aceptado mientras no haya consumidores serios; el salto a submodule+tags
  (primer consumidor real) es el momento de imponer versionado.
- La exigencia de compilación standalone del componente en CI detecta
  temprano cualquier fuga accidental de dependencias hacia `main/`, que sería
  el fallo de integración más probable y el más caro de destapar tarde.
