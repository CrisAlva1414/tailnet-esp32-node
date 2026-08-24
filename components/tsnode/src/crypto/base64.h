/*
 * Base64 encoder (minimal, for X-Tailscale-Handshake header).
 * No decoder needed — we only encode the Noise initiation.
 */

#ifndef TSNODE_BASE64_H
#define TSNODE_BASE64_H

#include <stddef.h>
#include <stdint.h>

/*
 * Encode binary data to base64 string.
 * out must have at least ((in_len + 2) / 3 * 4 + 1) bytes.
 * Returns the number of characters written (excluding null terminator).
 */
size_t tsnode_base64_encode(char *out, size_t out_size,
                            const uint8_t *in, size_t in_len);

#endif /* TSNODE_BASE64_H */
