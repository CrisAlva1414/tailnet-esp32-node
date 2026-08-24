# Convención de privacidad documental (ADR-0010)

Regla operativa: **todo lo versionado es público**. Antes de escribir un
archivo o comentario, clasificá su contenido con esta tabla.

## Clasificación

| Contenido | Dónde vive |
|---|---|
| Qué es la librería, arquitectura, ADRs, hallazgos de protocolo, formato | versionado (`docs/`, código) |
| Valores del protocolo Tailscale/WireGuard/Noise (públicos por definición) | versionado |
| Runbooks del despliegue personal, IPs asignadas a nodos propios, SSID/PSK reales, credenciales de entorno de prueba, topología LAN | `docs/private/` — gitignoreado, JAMÁS versionado |
| Código con endpoints/claves literales de cualquier entorno (incluidos tests) | prohibido; parámetros entran por consola/Kconfig |

## Placeholders obligatorios en docs públicas

| Dato real | Placeholder |
|---|---|
| SSID Wi-Fi | `<ssid>` |
| PSK / contraseña | `<psk>` |
| IP de tailnet (100.x.x.x) | `<ip-tailnet>` |
| IP LAN (192.168.x.x, 10.x.x.x) | `<ip-lan>` |
| Auth key / token | `<auth-key>` |
| Nombre de host/nodo que identifique el despliegue personal | `<hostname>` |
| Dirección física, número de unidad, nombres propios de red | `<ubicacion>` |

## Checklist antes de commitear/pushear docs

1. ¿El archivo contiene algún valor de la columna "dato real" de arriba sin
   placeholder? → sanitizar.
2. ¿Describe la red/hogar/entorno físico del operador? → mover a `docs/private/`.
3. ¿Cita claves o endpoints hardcoded en código? → refactorizar a parámetros.
4. Verificación mecánica final:

```
git grep -inE '(ssid|psk).{0,40}(=|:)\s*[`'"'"'][^<`'"'"']{3,}' -- '*.md' ':!docs/private'
git grep -nE '\b(100\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}|192\.168\.[0-9]{1,3}\.[0-9]{1,3})\b' -- '*.md' '*.c' '*.h' ':!docs/private'
```

Cualquier coincidencia se revisa a mano antes del push.

## Si igual se filtró algo

1. **Rotar la credencial expuesta inmediatamente** — no negociable, la
   exposición ya ocurrió aunque se borre el archivo.
2. Evaluar scrub de historia (`git-filter-repo --replace-text`) + force push.
3. Registrar el incidente en la sesión del día y, si amerita, en un ADR.
