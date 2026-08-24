/*
 * BLAKE2s host tests (unkeyed + keyed) against official vectors.
 *
 * Vector provenance: tests/unit/protocol_vectors/blake2s_vectors.h
 * (RFC 7693 Appendix B + BLAKE2 reference KAT + dual-implementation
 * vectors, all documented in that header).
 */

#include <stdio.h>
#include <string.h>

#include "blake2s.h"
#include "tsnode_err.h"

#include "blake2s_vectors.h"

static int failures;

static void hex_decode(const char *hex, uint8_t *out, size_t cap, size_t *len)
{
    size_t n = strlen(hex) / 2;
    if (n > cap) {
        printf("FATAL: vector larger than buffer\n");
        failures++;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned int b;
        if (sscanf(&hex[i * 2], "%2x", &b) != 1) {
            printf("FATAL: bad hex in vector\n");
            failures++;
            return;
        }
        out[i] = (uint8_t)b;
    }
    *len = n;
}

/* Feeds data through the streaming API splitting input at the given
 * boundary sizes to exercise buffering paths. */
static void hash_streamed(uint8_t out[32], const uint8_t *in, size_t inlen,
                          const size_t *chunks, size_t nchunks)
{
    tsnode_blake2s_ctx ctx;
    tsnode_blake2s_init(&ctx, 32);
    size_t off = 0;
    for (size_t i = 0; i < nchunks && off < inlen; i++) {
        size_t take = chunks[i] < (inlen - off) ? chunks[i] : (inlen - off);
        tsnode_blake2s_update(&ctx, in + off, take);
        off += take;
    }
    if (off < inlen) {
        tsnode_blake2s_update(&ctx, in + off, inlen - off);
    }
    tsnode_blake2s_final(&ctx, out, 32);
}

static void test_unkeyed(void)
{
    static const size_t odd_chunks[] = {1, 63, 64, 5};
    uint8_t in[256], got[32];
    size_t inlen;

    printf("blake2s unkeyed:\n");
    for (unsigned i = 0; i < BLAKE2S_UNKEYED_N; i++) {
        hex_decode(blake2s_unkeyed[i].in_hex, in, sizeof(in), &inlen);

        memset(got, 0xAA, sizeof(got));
        tsnode_blake2s(got, 32, in, inlen);
        /* one-shot */
        {
            char hex[65];
            for (int j = 0; j < 32; j++) {
                sprintf(&hex[j * 2], "%02x", got[j]);
            }
            if (strcmp(hex, blake2s_unkeyed[i].out_hex) != 0) {
                printf("  FAIL [%u] one-shot: got %s\n", i, hex);
                failures++;
                continue;
            }
        }
        /* streamed with odd chunking */
        hash_streamed(got, in, inlen, odd_chunks, 4);
        {
            char hex[65];
            for (int j = 0; j < 32; j++) {
                sprintf(&hex[j * 2], "%02x", got[j]);
            }
            if (strcmp(hex, blake2s_unkeyed[i].out_hex) != 0) {
                printf("  FAIL [%u] streamed: got %s\n", i, hex);
                failures++;
                continue;
            }
        }
        printf("  OK   [%u] len=%zu\n", i, inlen);
    }
}

static void test_keyed_32(void)
{
    uint8_t key[32], in[256], got[32];
    size_t keylen, inlen;
    char key_hex[65];

    hex_decode(BLAKE2S_KEYED_KEY_HEX, key, sizeof(key), &keylen);
    for (int j = 0; j < 32; j++) {
        sprintf(&key_hex[j * 2], "%02x", key[j]);
    }

    printf("blake2s keyed 32-byte out (official KAT subset):\n");
    for (unsigned i = 0; i < BLAKE2S_KEYED_N; i++) {
        hex_decode(blake2s_keyed[i].in_hex, in, sizeof(in), &inlen);
        if ((size_t)blake2s_keyed[i].in_len != inlen) {
            printf("  FAIL [%u] vector length mismatch\n", i);
            failures++;
            continue;
        }
        tsnode_err_t err = tsnode_blake2s_keyed(got, 32, key, keylen,
                                                in, inlen);
        if (err != TSNODE_OK) {
            printf("  FAIL [%u] init_key error %s\n", i,
                   tsnode_err_name(err));
            failures++;
            continue;
        }
        char hex[65];
        for (int j = 0; j < 32; j++) {
            sprintf(&hex[j * 2], "%02x", got[j]);
        }
        if (strcmp(hex, blake2s_keyed[i].out_hex) != 0) {
            printf("  FAIL [%u] len=%zu: got %s\n", i, inlen, hex);
            failures++;
            continue;
        }
        printf("  OK   [%u] len=%zu\n", i, inlen);
    }
    (void)key_hex;
}

static void test_keyed_16(void)
{
    uint8_t key[32], in[256], got32[32], got16[16];
    size_t keylen, inlen;

    hex_decode(BLAKE2S_KEYED_KEY_HEX, key, sizeof(key), &keylen);

    printf("blake2s keyed 16-byte out (dual-implementation vectors):\n");
    for (unsigned i = 0; i < BLAKE2S_KEYED16_N; i++) {
        hex_decode(blake2s_keyed16[i].in_hex, in, sizeof(in), &inlen);
        tsnode_err_t err = tsnode_blake2s_keyed(got16, 16, key, keylen,
                                                in, inlen);
        if (err != TSNODE_OK) {
            printf("  FAIL [%u] init_key error %s\n", i,
                   tsnode_err_name(err));
            failures++;
            continue;
        }
        char hex[33];
        for (int j = 0; j < 16; j++) {
            sprintf(&hex[j * 2], "%02x", got16[j]);
        }
        if (strcmp(hex, blake2s_keyed16[i].out_hex) != 0) {
            printf("  FAIL [%u] len=%zu: got %s\n", i, inlen, hex);
            failures++;
            continue;
        }
        /* digest_size is a parameter, not truncation of the 32-byte output */
        (void)tsnode_blake2s_keyed(got32, 32, key, keylen, in, inlen);
        if (memcmp(got16, got32, 16) == 0) {
            printf("  FAIL [%u] 16-out equals truncation of 32-out "
                   "(RFC 7693 semantics broken)\n", i);
            failures++;
            continue;
        }
        printf("  OK   [%u] len=%zu\n", i, inlen);
    }
}

static void test_param_validation(void)
{
    uint8_t key[32] = {0}, out[32];
    tsnode_blake2s_ctx ctx;

    printf("blake2s keyed parameter validation:\n");

    if (tsnode_blake2s_init_key(&ctx, 32, key, 0) != TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: keylen=0 accepted\n");
        failures++;
    } else {
        printf("  OK   keylen=0 rejected\n");
    }
    if (tsnode_blake2s_init_key(&ctx, 32, key, 33) !=
        TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: keylen=33 accepted\n");
        failures++;
    } else {
        printf("  OK   keylen=33 rejected\n");
    }
    if (tsnode_blake2s_init_key(&ctx, 0, key, 32) != TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: outlen=0 accepted\n");
        failures++;
    } else {
        printf("  OK   outlen=0 rejected\n");
    }
    if (tsnode_blake2s_init_key(&ctx, 33, key, 32) !=
        TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: outlen=33 accepted\n");
        failures++;
    } else {
        printf("  OK   outlen=33 rejected\n");
    }
    if (tsnode_blake2s_init_key(NULL, 32, key, 32) !=
        TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: NULL ctx accepted\n");
        failures++;
    } else {
        printf("  OK   NULL ctx rejected\n");
    }
    if (tsnode_blake2s_keyed(out, 32, NULL, 32, key, 1) !=
        TSNODE_ERR_INVALID_ARG) {
        printf("  FAIL: NULL key accepted by one-shot\n");
        failures++;
    } else {
        printf("  OK   NULL key rejected\n");
    }
}

int main(void)
{
    test_unkeyed();
    test_keyed_32();
    test_keyed_16();
    test_param_validation();

    if (failures) {
        printf("\nBLAKE2S TESTS: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nBLAKE2S TESTS: ALL PASS\n");
    return 0;
}
