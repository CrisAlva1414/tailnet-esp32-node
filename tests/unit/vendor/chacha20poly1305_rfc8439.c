/*
 * Test-only ChaCha20-Poly1305 implementation — see the header. Uses
 * poly1305-donna for the MAC; the ChaCha20 core is written here from
 * RFC 8439 §2.3 and validated against §2.3.2/§2.8.2 vectors.
 */

#include "chacha20poly1305_rfc8439.h"

#include <string.h>

#include "poly1305-donna.h"

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32u - (n))))

static uint32_t load32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Quarter round, RFC 8439 §2.1. */
static void qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b;
    *d ^= *a;
    *d = ROTL32(*d, 16);
    *c += *d;
    *b ^= *c;
    *b = ROTL32(*b, 12);
    *a += *b;
    *d ^= *a;
    *d = ROTL32(*d, 8);
    *c += *d;
    *b ^= *c;
    *b = ROTL32(*b, 7);
}

void t_chacha20_block(uint8_t out[64], const uint8_t key[32],
                      const uint8_t nonce[12], uint32_t counter)
{
    static const uint32_t constants[4] = { 0x61707865u, 0x3320646eu,
                                           0x79622d32u, 0x6b206574u };
    uint32_t st[16], x[16];

    memcpy(st, constants, sizeof(constants));
    for (unsigned i = 0; i < 8; i++) {
        st[4u + i] = load32le(key + 4u * i);
    }
    st[12] = counter;
    st[13] = load32le(nonce);
    st[14] = load32le(nonce + 4u);
    st[15] = load32le(nonce + 8u);

    memcpy(x, st, sizeof(st));
    for (unsigned r = 0; r < 10; r++) {
        qr(&x[0], &x[4], &x[8], &x[12]);
        qr(&x[1], &x[5], &x[9], &x[13]);
        qr(&x[2], &x[6], &x[10], &x[14]);
        qr(&x[3], &x[7], &x[11], &x[15]);
        qr(&x[0], &x[5], &x[10], &x[15]);
        qr(&x[1], &x[6], &x[11], &x[12]);
        qr(&x[2], &x[7], &x[8], &x[13]);
        qr(&x[3], &x[4], &x[9], &x[14]);
    }
    for (unsigned i = 0; i < 16; i++) {
        store32le(out + 4u * i, x[i] + st[i]);
    }
}

static void pad16(poly1305_context *ctx, size_t len)
{
    static const uint8_t zeros[16] = { 0 };
    size_t rem = len % 16u;
    if (rem != 0) {
        poly1305_update(ctx, zeros, 16u - rem);
    }
}

static void mac_aead(uint8_t mac[16], const uint8_t poly_key[32],
                     const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                     size_t ct_len)
{
    poly1305_context ctx;
    poly1305_init(&ctx, poly_key);

    if (aad_len > 0 && aad != NULL) {
        poly1305_update(&ctx, aad, aad_len);
    }
    pad16(&ctx, aad_len);
    if (ct_len > 0 && ct != NULL) {
        poly1305_update(&ctx, ct, ct_len);
    }
    pad16(&ctx, ct_len);

    uint8_t lens[16];
    memset(lens, 0, sizeof(lens));
    /* Lengths are LE64; sizes here are far below 2^32 but keep full
     * 64-bit serialization anyway. */
    for (unsigned i = 0; i < 8; i++) {
        lens[i] = (uint8_t)(((uint64_t)aad_len >> (8u * i)) & 0xFFu);
        lens[8u + i] = (uint8_t)(((uint64_t)ct_len >> (8u * i)) & 0xFFu);
    }
    poly1305_update(&ctx, lens, sizeof(lens));
    poly1305_finish(&ctx, mac);
}

/* XOR pt with keystream starting at block counter `start`. */
static void keystream_xor(uint8_t *dst, const uint8_t *src, size_t len,
                          const uint8_t key[32], const uint8_t nonce[12],
                          uint32_t start)
{
    uint8_t block[64];
    uint32_t ctr = start;
    for (size_t off = 0; off < len; off += 64u) {
        t_chacha20_block(block, key, nonce, ctr++);
        size_t n = len - off > 64u ? 64u : len - off;
        for (size_t i = 0; i < n; i++) {
            dst[off + i] = (uint8_t)(src[off + i] ^ block[i]);
        }
    }
}

void t_aead_seal(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
                 const uint8_t *aad, size_t aad_len, const uint8_t *pt,
                 size_t pt_len)
{
    uint8_t poly_key[32];
    uint8_t block[64];
    t_chacha20_block(block, key, nonce, 0);
    memcpy(poly_key, block, 32);

    keystream_xor(out, pt, pt_len, key, nonce, 1);

    uint8_t mac[16];
    mac_aead(mac, poly_key, aad, aad_len, out, pt_len);
    memcpy(out + pt_len, mac, 16);

    memset(poly_key, 0, sizeof(poly_key));
    memset(block, 0, sizeof(block));
}

int t_aead_open(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
                const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                size_t ct_len)
{
    if (ct_len < 16u) {
        return -1;
    }
    size_t pt_len = ct_len - 16u;

    uint8_t poly_key[32];
    uint8_t block[64];
    t_chacha20_block(block, key, nonce, 0);
    memcpy(poly_key, block, 32);

    uint8_t expected[16];
    mac_aead(expected, poly_key, aad, aad_len, ct, pt_len);

    /* Constant-time tag compare even in test code (habit per AGENTS.md
     * §4). */
    volatile uint8_t diff = 0;
    for (unsigned i = 0; i < 16; i++) {
        diff |= (uint8_t)(expected[i] ^ ct[pt_len + i]);
    }

    int rc = diff == 0 ? 0 : -1;
    if (rc == 0) {
        keystream_xor(out, ct, pt_len, key, nonce, 1);
    } else {
        memset(out, 0, pt_len);
    }

    memset(poly_key, 0, sizeof(poly_key));
    memset(block, 0, sizeof(block));
    return rc;
}
