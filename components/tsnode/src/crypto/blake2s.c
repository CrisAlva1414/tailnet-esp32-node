/*
 * BLAKE2s — clean-room implementation from RFC 7693.
 *
 * This implements only the unkeyed hash variant (1..32 byte output).
 * All constants, the IV, and the sigma permutation are taken directly
 * from RFC 7693 Sections 2.6-2.7.
 *
 * No dynamic allocation. No platform headers. Pure C11.
 */

#include "blake2s.h"

#include <string.h>

/* RFC 7693 Section 2.6 */
static const uint32_t blake2s_IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};

/* RFC 7693 Section 2.7 — sigma permutation for 10 rounds */
static const uint8_t sigma[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

/* RFC 7693 Section 3.2 — G mixing function */
static inline uint32_t rotr32(uint32_t w, unsigned c)
{
    return (w >> c) | (w << (32 - c));
}

static void G(uint32_t *v, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
              uint32_t x, uint32_t y)
{
    v[a] = v[a] + v[b] + x;
    v[d] = rotr32(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 12);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr32(v[d] ^ v[a], 8);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 7);
}

static void compress(tsnode_blake2s_ctx *ctx, const uint8_t block[64])
{
    uint32_t v[16], m[16];

    /* Initialize working vector */
    for (int i = 0; i < 8; i++) {
        v[i]     = ctx->h[i];
        v[i + 8] = blake2s_IV[i];
    }

    /* RFC 7693 Section 2.5: mix in t, f, and salt (zero for unkeyed) */
    v[12] ^= (uint32_t)ctx->t[0];
    v[13] ^= (uint32_t)(ctx->t[0] >> 32);
    v[14] ^= (uint32_t)ctx->t[1];
    v[15] ^= (uint32_t)(ctx->t[1] >> 32);

    /* Finalization flag */
    if (ctx->f[0]) {
        v[14] = ~v[14];  /* invert all bits of v[14] */
    }

    /* Load message block as 16 little-endian 32-bit words */
    for (int i = 0; i < 16; i++) {
        m[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }

    /* 10 rounds of mixing */
    for (int i = 0; i < 10; i++) {
        const uint8_t *s = sigma[i];
        G(v,  0,  4,  8, 12, m[s[ 0]], m[s[ 1]]);
        G(v,  1,  5,  9, 13, m[s[ 2]], m[s[ 3]]);
        G(v,  2,  6, 10, 14, m[s[ 4]], m[s[ 5]]);
        G(v,  3,  7, 11, 15, m[s[ 6]], m[s[ 7]]);
        G(v,  0,  5, 10, 15, m[s[ 8]], m[s[ 9]]);
        G(v,  1,  6, 11, 12, m[s[10]], m[s[11]]);
        G(v,  2,  7,  8, 13, m[s[12]], m[s[13]]);
        G(v,  3,  4,  9, 14, m[s[14]], m[s[15]]);
    }

    /* Update chaining state */
    for (int i = 0; i < 8; i++) {
        ctx->h[i] ^= v[i] ^ v[i + 8];
    }
}

void tsnode_blake2s_init(tsnode_blake2s_ctx *ctx, size_t outlen)
{
    /* Parameter block: fanout=1, depth=1, no salt, no key, no personalization.
     * digest_length = outlen, but we only support 1..32. */
    if (outlen == 0 || outlen > TSNODE_BLAKE2S_OUTBYTES) {
        return;  /* caller must check; invalid param */
    }
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 8; i++) {
        ctx->h[i] = blake2s_IV[i];
    }
    /* XOR parameter block into h[0]: digest_length | fanout<<8 | depth<<16 */
    ctx->h[0] ^= (uint32_t)outlen | (1u << 8) | (1u << 16);
    ctx->outlen = outlen;
}

void tsnode_blake2s_update(tsnode_blake2s_ctx *ctx, const uint8_t *in,
                           size_t inlen)
{
    if (inlen == 0) {
        return;
    }

    size_t left = ctx->buflen;
    size_t fill = TSNODE_BLAKE2S_BLOCKBYTES - left;

    /* If enough data to complete a block, compress it */
    if (inlen > fill) {
        ctx->buflen = 0;
        /* Increment byte counter by block size */
        ctx->t[0] += TSNODE_BLAKE2S_BLOCKBYTES;
        if (ctx->t[0] < TSNODE_BLAKE2S_BLOCKBYTES) {
            ctx->t[1]++;
        }
        memcpy(ctx->buf + left, in, fill);
        compress(ctx, ctx->buf);
        in += fill;
        inlen -= fill;

        /* Compress complete blocks directly from input */
        while (inlen > TSNODE_BLAKE2S_BLOCKBYTES) {
            ctx->t[0] += TSNODE_BLAKE2S_BLOCKBYTES;
            if (ctx->t[0] < TSNODE_BLAKE2S_BLOCKBYTES) {
                ctx->t[1]++;
            }
            compress(ctx, in);
            in += TSNODE_BLAKE2S_BLOCKBYTES;
            inlen -= TSNODE_BLAKE2S_BLOCKBYTES;
        }
    }

    /* Buffer remaining bytes */
    memcpy(ctx->buf + ctx->buflen, in, inlen);
    ctx->buflen += inlen;
}

void tsnode_blake2s_final(tsnode_blake2s_ctx *ctx, uint8_t *out,
                          size_t outlen)
{
    /* Pad with zeros */
    memset(ctx->buf + ctx->buflen, 0,
           TSNODE_BLAKE2S_BLOCKBYTES - ctx->buflen);

    /* Increment counter by buffered bytes */
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) {
        ctx->t[1]++;
    }

    /* Set finalization flag */
    ctx->f[0] = (uint64_t)0xFFFFFFFFFFFFFFFF;

    compress(ctx, ctx->buf);

    /* Produce output: first outlen bytes of h[] as little-endian */
    size_t to_write = outlen < ctx->outlen ? outlen : ctx->outlen;
    for (size_t i = 0; i < to_write; i++) {
        out[i] = (uint8_t)(ctx->h[i / 4] >> (8 * (i % 4)));
    }
}

void tsnode_blake2s(uint8_t *out, size_t outlen, const uint8_t *in,
                    size_t inlen)
{
    tsnode_blake2s_ctx ctx;
    tsnode_blake2s_init(&ctx, outlen);
    tsnode_blake2s_update(&ctx, in, inlen);
    tsnode_blake2s_final(&ctx, out, outlen);
}
