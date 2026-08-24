# 2026-08-24 — Noise handshake fixes and record layer prebuffer

## Contexto
El Noise handshake no completaba (tag verification fallaba). Investigación profunda del protocolo Noise IK + register/map flow.

## Cambios

### BLAKE2s parameter block fix (CRÍTICO)
- `blake2s.c`: XOR del parameter block corregido de `(1u << 8)` a `(1u << 24)`.
- El byte 2 del parameter block era `key_length` (debía ser 0), no `depth`.
- Con `(1u << 8)`: depth se seteaba en key_length (byte 1) → depth=1, key_length=0. Eso estaba bien para la handshake pero mal para la chiffre.
- El fix correcto es: byte 0=digest_length, byte 1=key_length(0), byte 2=fanout(1), byte 3=depth(1).
- XOR en h[0]: `digest_length | (fanout << 16) | (depth << 24)` → `(1u << 24)`.
- **Resultado**: El servidor Tailscale ahora acepta y responde al Noise initiation.

### noise_split: HKDF-Extract step
- `ts2021.c:noise_split()`: Ahora hace `HKDF-Extract(salt=ck, IKM=nil)` primero, luego `HKDF-Expand(prk, nil, 64)`.
- Antes saltaba el Extract y usaba ck directamente como PRK.
- Go: `hkdf.New(newBLAKE2s, nil, s.ck[:], nil)` = Extract + Expand.

### Split key derivation simplificada
- Eliminada la DH X25519 spurious del handshake_complete.
- Ahora usa `noise_split()` directamente para derivar tx/rx keys.

### Prologue format
- De bytes binarios `(uint8_t)(version >> 8)` a string decimal ASCII `"145"`.

### Protocol version
- De `1` a `145` (CurrentCapabilityVersion de Tailscale).

### Machine/Node key clamping
- `tsnode_x25519_keygen()` usa `mbedtls_ecp_gen_keypair` → clamping RFC 7748 interno.

### HTTP extra bytes → prebuffer
- **Bug**: El HTTP read loop consumía 124 bytes (51 Noise + 73 server transport frames).
- Los 73 bytes se descartaban y el record layer leía basura del socket.
- **Fix**: `ts2021_conn_t` tiene ahora `prebuf[512]` + `prebuf_len`.
- `ts2021_conn_read()` lee de prebuf primero, luego del socket.
- `ts2021_record_recv()` usa `ts2021_conn_read()` en vez de `tsnode_port_socket_read()` directo.

### ChaCha20-Poly1305 API
- Confirmada API mbedTLS 3.6.3: `setkey(ctx, key)` 2 args, `encrypt_and_tag(ctx, len, nonce, aad, alen, in, out, tag)` 8 args, `auth_decrypt(ctx, len, nonce, aad, alen, tag, in, out)` 8 args.

### base64 encoder
- `components/tsnode/src/crypto/base64.c/.h` — `tsnode_base64_encode()` para X-Tailscale-Handshake header.

### WiFi
- SSID: `REDACTED-SSID`, PSK: `REDACTED-PSK`, IP: `<ip-lan>`.

### Console
- Comandos: `tsconnect`, `wifi set <ssid>`, `tskey set`.

## Resultado
- Noise handshake completa (state → 3).
- RegisterRequest se envía (157 bytes encrypted).
- RegisterResponse recibido y decryptado: `machine not yet authorized, waiting...`
- MapRequest enviado, MapResponse recibido: `0 peers, self=`
- **El protocolo completo funciona end-to-end.**

## Pendiente
- Autorizar nodo en admin de Tailscale (https://login.tailscale.com/admin/machines).
- Agregar loop de MapRequest/Response polling para mantener sesión viva.
- Asignación de IP 100.x.x.x tras autorización.
- WireGuard data plane (encapsular/decapsular tráfico WG).
