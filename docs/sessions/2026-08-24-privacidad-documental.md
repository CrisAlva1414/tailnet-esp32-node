# 2026-08-24 — política de privacidad documental y remediación de exposición

## Contexto

Auditoría de exposición del repo público (pedido explícito del usuario:
separar documentación pública del proyecto de la información personal del
operador). Se encontraron credenciales reales en docs versionadas y en la
historia de git ya pusheada a GitHub.

## Cambios

- ADR-0010 creado y aceptado: separación público/privado de la documentación.
- `docs/private/` creado (gitignoreado): runbook de reactivación movido ahí
  (`git rm --cached`); contiene IPs del despliegue propio.
- Sanitización de docs públicas: sesiones 2026-08-20/24 y ADR-0009 — SSID,
  PSK, IP de tailnet e IP LAN reemplazados por placeholders/referencias a
  doc privada.
- `main/console.c`: `tsconnectlocal` deja de tener host/puerto/key hardcoded;
  ahora recibe `<host> <port> [key-hex]` por consola (ADR-0010 §5).
- `.gitignore`: bloque nuevo para `docs/private/`.
- AGENTS.md actualizado (§3 estructura, §7 formato de sesión) con la regla
  transversal de privacidad; README con sección de política de privacidad.
- `docs/format/documentation-privacy.md`: clasificación, placeholders y
  checklist pre-commit.

## Decisiones de seguridad tomadas o revisadas

- **Credencial Wi-Fi expuesta en historia pública → rotación obligatoria por
  parte del operador** (fuera de alcance del repo; registrada como acción
  pendiente del operador). El scrub de historia es defensa en profundidad, no
  sustituye rotación.
- Scrub de historia con `git-filter-repo --replace-text` + force push,
  decidido por el usuario en el chat (repo joven, ~15 commits, costo mínimo).
- Regla nueva permanente: ningún endpoint/clave literal de ningún entorno en
  el código fuente, incluidos comandos test-only.

## Pendiente / bloqueado

- Operador: rotar PSK Wi-Fi y verificar que ninguna auth key histórica siga
  vigente (las de la sesión anterior ya rotaron dos veces).
- Operador opcional: pedir a GitHub support purga de objetos inalcanzables si
  quiere eliminar residuos cacheados post-scrub.
