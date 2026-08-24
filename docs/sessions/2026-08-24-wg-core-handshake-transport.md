# 2026-08-24 — WireGuard core handshake + transport + routing

## Contexto
Sprint acordado para implementar el núcleo WireGuard (data plane) como
módulo autocontenido: código + tests host + verificación contra fuentes
primarias. Objetivo: handshake IKpsk2, transport ChaCha20-Poly1305,
anti-replay, cryptokey routing IPv4 — todo verde en tests host.

## Cambios
- `crypto/hmac_blake2s.{c,h}`: HMAC-BLAKE2s + HKDF 1/2/3 bloques,
  extraído de ts2021.c (deduplicación de patrón de KDF compartido).
- `wg/wg.{c,h}`: WireGuard core completo — handshake IKpsk2
  (create/consume initiation + response), transport encap/decap,
  cryptokey routing IPv4 longest-prefix, vtable de crypto inyectable.
- `wg/wg_crypto_mbedtls.c`: backend producción (mbedTLS ChaCha20-Poly1305
  + x25519_wrapper + tsnode_port_random_bytes).
- `proto/ts2021.c`: refactorizado para usar hmac_blake2s compartido.
- `include/tsnode_err.h`: +TSNODE_ERR_REPLAY.
- `tests/unit/test_wg.c`: 70+ checks — vectores RFC 7748/8439, roundtrip
  handshake (ambos roles), transport both ways, keepalives, routing,
  rekey, hostile inputs.
- `tests/unit/vendor/*`: doubles host (TweetNaCl X25519, poly1305-donna,
  ChaCha20 RFC 8439, LCG RNG).
- `tests/unit/protocol_vectors/wg_vectors.h`: vectores oficiales.

Commits:
- `9bbb9aa` feat(wg): add WireGuard core handshake, transport, and
  cryptokey routing (23 archivos, +4203 líneas).

## Decisiones de seguridad tomadas o revisadas
- **MAC1 verification**: corregido bug donde el responder verificaba con
  peer->mac1_key (hash de la otra pubkey) en vez de hash("mac1----" ||
  own_pub). El initiator y el responder deben usar la misma mac1_key
  (la del receiver).
- **Transport receiver index**: el receiver index en el header de
  transporte está en offset 4, no en offset 8 (que es para handshake).
  Corregido el lookup en decap.
- **Auth-before-state**: AEAD open se ejecuta ANTES de tocar el replay
  window (verificado contra wireguard-go receive.go:448-453). Paquetes
  forjados no contaminan el filtro.
- **Cookie replies (mac2)**: documentado como limitación conocida de v1.
  Consume requiere XChaCha20 (HChaCha20), pendiente de incremento
  futuro antes de despliegue fuera de red controlada.
- **Single session per peer**: rekey reemplaza sesión inmediatamente
  (sin dual-session para packets in-flight). Tradeoff aceptado para v1.
- **Timestamps**: caller provee TAI64N; core solo exige estricto
  crecimiento (no tiene acceso a clock, ADR-0008 D4).

## Pendiente / bloqueado
- **cppcheck**: completado limpio (sin findings en wg/ ni main/).
  Instalación requerida `sudo apt install cppcheck`.
- **Interop vs wg-quick local**: requiere `sudo ip link add wg0 type
  wireguard` — pendiente para hardware sprint.
- **Session doc en `docs/sessions/`** + **ops privados en
  `docs/private/`**: documentación sanitizada pendiente de escribir
  (ADR-0010).
- **HChaCha20 / cookie reply consume**: ADR futuro antes de
  implementar.
