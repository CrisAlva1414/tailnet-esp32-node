/*
 * Base64 encoder — clean-room implementation for protocol headers.
 */

#include "base64.h"

#include <string.h>

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t tsnode_base64_encode(char *out, size_t out_size,
                            const uint8_t *in, size_t in_len)
{
    if (out == NULL || in == NULL) return 0;

    size_t needed = ((in_len + 2) / 3) * 4 + 1;
    if (out_size < needed) return 0;

    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t a = (uint32_t)in[i];
        uint32_t b = (i + 1 < in_len) ? (uint32_t)in[i + 1] : 0;
        uint32_t c = (i + 2 < in_len) ? (uint32_t)in[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < in_len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}
