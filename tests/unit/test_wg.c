/*
 * Host unit tests for the WireGuard core (wg.c) — ADR-0008.
 *
 * Layers verified here:
 *  1. Primitive vectors: X25519 (RFC 7748 §5.2), ChaCha20 block and
 *     AEAD (RFC 8439 §2.3.2/§2.8.2) against the test-only backend.
 *  2. Handshake roundtrip between two devices through the real wg.c
 *     state machine (both roles), then transport data both ways,
 *     keepalives, cryptokey routing.
 *  3. Hostile inputs: truncation, wrong types, tampered bytes, forged
 *     transport packets, replays, stale timestamps, unknown indices —
 *     everything must fail closed without corrupting device state.
 */

#include <stdio.h>
#include <string.h>

#include "protocol_vectors/wg_vectors.h"
#include "vendor/chacha20poly1305_rfc8439.h"
#include "vendor/wg_crypto_host.h"
#include "src/wg/wg.h"

static int failures = 0;

/* Wire format offsets — match WG constants in wg.c (wire-verified
 * against noise-types.go message layouts). */
#define TEST_INIT_OFF_STATIC 40u /* encrypted static pubkey offset */

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s (line %d)\n", name, __LINE__);                     \
            failures++;                                                        \
        } else {                                                               \
            printf("ok   %s\n", name);                                         \
        }                                                                      \
    } while (0)

static void hexdump_line(const char *label, const uint8_t *buf, size_t len)
{
    printf("  %s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }
    printf("\n");
}

/* ---- 1. Primitive vectors ---- */

static void test_x25519_vector(void)
{
    const uint8_t a_priv[32] = VEC7748_ALICE_PRIV;
    const uint8_t a_pub[32] = VEC7748_ALICE_PUB;
    const uint8_t b_priv[32] = VEC7748_BOB_PRIV;
    const uint8_t b_pub[32] = VEC7748_BOB_PUB;
    const uint8_t expected[32] = VEC7748_SHARED;
    uint8_t derived[32];

    const tsnode_wg_crypto_t *cr = tsnode_wg_crypto_host();
    CHECK(cr->pubkey(derived, a_priv) == TSNODE_OK &&
              memcmp(derived, a_pub, 32) == 0,
          "x25519 pubkey(alice) == RFC 7748 vector");
    CHECK(cr->pubkey(derived, b_priv) == TSNODE_OK &&
              memcmp(derived, b_pub, 32) == 0,
          "x25519 pubkey(bob) == RFC 7748 vector");
    /* DH is commutative; one direction suffices to verify correctness. */
    CHECK(cr->dh(derived, a_priv, b_pub) == TSNODE_OK &&
              memcmp(derived, expected, 32) == 0,
          "x25519 DH(a_priv, b_pub) == RFC 7748 shared secret");
    CHECK(cr->dh(derived, b_priv, a_pub) == TSNODE_OK &&
              memcmp(derived, expected, 32) == 0,
          "x25519 DH(b_priv, a_pub) == RFC 7748 shared secret");
}

static void test_chacha20_block_vector(void)
{
    const uint8_t key[32] = VEC8439_BLOCK_KEY;
    const uint8_t nonce[12] = VEC8439_BLOCK_NONCE;
    const uint8_t expected[64] = VEC8439_BLOCK_OUT;
    uint8_t out[64];

    t_chacha20_block(out, key, nonce, VEC8439_BLOCK_COUNTER);
    if (memcmp(out, expected, 64) != 0) {
        hexdump_line("got     ", out, 64);
        hexdump_line("expected", expected, 64);
        CHECK(false, "chacha20 block == RFC 8439 2.3.2");
    } else {
        CHECK(true, "chacha20 block == RFC 8439 2.3.2");
    }
}

static void test_aead_vector(void)
{
    const uint8_t key[32] = VEC8439_AEAD_KEY;
    const uint8_t nonce[12] = VEC8439_AEAD_NONCE;
    const uint8_t aad[12] = VEC8439_AEAD_AAD;
    const uint8_t pt[] = VEC8439_AEAD_PT;
    const uint8_t ct_expected[] = VEC8439_AEAD_CT;
    const uint8_t tag_expected[16] = VEC8439_AEAD_TAG;
    size_t pt_len = sizeof(pt);

    uint8_t out[256];
    t_aead_seal(out, key, nonce, aad, sizeof(aad), pt, pt_len);
    bool seal_ok =
        memcmp(out, ct_expected, pt_len) == 0 &&
        memcmp(out + pt_len, tag_expected, 16) == 0;
    if (!seal_ok) {
        hexdump_line("ct got     ", out, pt_len + 16);
    }
    CHECK(seal_ok, "aead seal == RFC 8439 2.8.2");

    uint8_t dec[128];
    CHECK(t_aead_open(dec, key, nonce, aad, sizeof(aad), out, pt_len + 16) ==
                  0 &&
              memcmp(dec, pt, pt_len) == 0,
          "aead open roundtrip == RFC 8439 2.8.2");

    /* Tampered tag must fail. */
    out[pt_len] ^= 0x01u;
    memset(dec, 0xAA, sizeof(dec));
    CHECK(t_aead_open(dec, key, nonce, aad, sizeof(aad), out, pt_len + 16) ==
              -1,
          "aead open rejects tampered tag");
}

/* ---- Test fixture: two linked devices ---- */

typedef struct {
    tsnode_wg_device_t dev;
    int peer_idx; /* index of the OTHER side within this device */
    uint8_t priv[32];
    uint8_t pub[32];
} node_t;

static void node_setup(node_t *n, const char *seed_tag, uint64_t rng_seed)
{
    /* Deterministic static keys derived from the seed tag (tests only;
     * firmware keys come from provisioning). */
    memset(n->priv, 0, 32);
    memcpy(n->priv, seed_tag, strlen(seed_tag));
    for (unsigned i = 0; i < 32; i++) {
        n->priv[i] ^= (uint8_t)(rng_seed >> (i % 8));
        n->priv[i] = (uint8_t)(n->priv[i] * 31u + 17u);
    }

    tsnode_test_host_rng_seed(rng_seed ^ 0xDEADBEEFull);
    const tsnode_wg_crypto_t *cr = tsnode_wg_crypto_host();
    CHECK(cr->pubkey(n->pub, n->priv) == TSNODE_OK, "fixture pubkey derive");
    n->peer_idx = -1;
}

static void link_nodes(node_t *a, node_t *b, uint32_t a_net, uint32_t b_net)
{
    tsnode_wg_peer_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.public_key, b->pub, 32); /* a knows b */
    cfg.allowed_ip = a_net;
    cfg.allowed_mask = 0xFFFFFF00u;
    CHECK((a->peer_idx = tsnode_wg_peer_add(&a->dev, &cfg)) >= 0,
          "peer add a->b");

    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.public_key, a->pub, 32); /* b knows a */
    cfg.allowed_ip = b_net;
    cfg.allowed_mask = 0xFFFFFF00u;
    CHECK((b->peer_idx = tsnode_wg_peer_add(&b->dev, &cfg)) >= 0,
          "peer add b->a");
}

/* Full handshake: a initiates, b responds, both end with sessions. */
static void handshake(node_t *initiator, node_t *responder,
                      const uint8_t timestamp[12], uint64_t now_ms)
{
    uint8_t init_pkt[TSNODE_WG_INITIATION_LEN];
    uint8_t resp_pkt[TSNODE_WG_RESPONSE_LEN];
    int peer_idx = -1;

    CHECK(tsnode_wg_create_initiation(&initiator->dev, initiator->peer_idx,
                                      timestamp, init_pkt) == TSNODE_OK,
          "create_initiation ok");
    CHECK(tsnode_wg_consume_initiation(&responder->dev, init_pkt,
                                       sizeof(init_pkt),
                                       &peer_idx) == TSNODE_OK,
          "consume_initiation ok");
    CHECK(peer_idx == responder->peer_idx,
          "responder routes initiation to right peer");
    CHECK(tsnode_wg_create_response(&responder->dev, peer_idx, now_ms,
                                    resp_pkt) == TSNODE_OK,
          "create_response ok");
    CHECK(tsnode_wg_consume_response(&initiator->dev, resp_pkt,
                                     sizeof(resp_pkt), now_ms,
                                     &peer_idx) == TSNODE_OK,
          "consume_response ok");
    CHECK(peer_idx == initiator->peer_idx,
          "initiator routes response to right peer");
    CHECK(tsnode_wg_peer_has_session(&initiator->dev, initiator->peer_idx) &&
              tsnode_wg_peer_has_session(&responder->dev,
                                         responder->peer_idx),
          "both peers have sessions after handshake");
}

/* ---- 2. Roundtrip tests ---- */

static void test_handshake_roundtrip_and_data(void)
{
    node_t a, b;
    const uint8_t ts[12] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    uint8_t enc[1600], dec[1400];
    size_t enc_len, dec_len;
    int peer_idx = -1;
    const char payload[] = "hello wireguard over tsnode";

    node_setup(&a, "node-a-static-key-seed", 0x1111111111111111ull);
    node_setup(&b, "node-b-static-key-seed", 0x2222222222222222ull);
    CHECK(tsnode_wg_device_init(&a.dev, a.priv, tsnode_wg_crypto_host()) ==
              TSNODE_OK,
          "device init a");
    CHECK(tsnode_wg_device_init(&b.dev, b.priv, tsnode_wg_crypto_host()) ==
              TSNODE_OK,
          "device init b");
    link_nodes(&a, &b, 0x0A000001u /* 10.0.0.1 */, 0x0A000002u);

    handshake(&a, &b, ts, 1000);

    /* Data plane a -> b. */
    CHECK(tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)payload,
                          sizeof(payload), 1001, enc, sizeof(enc),
                          &enc_len) == TSNODE_OK,
          "encap a->b ok");
    CHECK(enc_len == TSNODE_WG_TRANSPORT_OVERHEAD + sizeof(payload),
          "encap length exact");
    CHECK(tsnode_wg_decap(&b.dev, enc, enc_len, dec, sizeof(dec), &dec_len,
                          &peer_idx) == TSNODE_OK,
          "decap at b ok");
    CHECK(dec_len == sizeof(payload) && memcmp(dec, payload, dec_len) == 0,
          "decrypted payload matches");

    /* Data plane b -> a over the same sessions (opposite keys). */
    CHECK(tsnode_wg_encap(&b.dev, b.peer_idx, (const uint8_t *)payload,
                          sizeof(payload), 1002, enc, sizeof(enc),
                          &enc_len) == TSNODE_OK,
          "encap b->a ok");
    CHECK(tsnode_wg_decap(&a.dev, enc, enc_len, dec, sizeof(dec), &dec_len,
                          &peer_idx) == TSNODE_OK,
          "decap at a ok");
    CHECK(memcmp(dec, payload, dec_len) == 0, "reverse direction matches");

    /* Keepalive: empty payload produces exactly 32 bytes. */
    CHECK(tsnode_wg_encap(&a.dev, a.peer_idx, NULL, 0, 1003, enc,
                          sizeof(enc), &enc_len) == TSNODE_OK &&
              enc_len == 32,
          "keepalive is 32 bytes");
    CHECK(tsnode_wg_decap(&b.dev, enc, enc_len, dec, sizeof(dec), &dec_len,
                          &peer_idx) == TSNODE_OK &&
              dec_len == 0,
          "keepalive decrypts to empty payload");

    /* Counters advance monotonically in the header. */
    uint8_t e1[64], e2[64];
    size_t l1, l2;
    (void)tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"x", 1, 1004,
                          e1, sizeof(e1), &l1);
    (void)tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"y", 1, 1005,
                          e2, sizeof(e2), &l2);
    uint64_t c1 = 0, c2 = 0;
    for (unsigned i = 0; i < 8; i++) {
        c1 |= (uint64_t)e1[8 + i] << (8u * i);
        c2 |= (uint64_t)e2[8 + i] << (8u * i);
    }
    CHECK(c2 == c1 + 1, "transport counters increment by one");

    /* Cryptokey routing. */
    CHECK(tsnode_wg_route(&b.dev, 0x0A000064u) >= 0,
          "route finds peer inside allowed prefix");
    CHECK(tsnode_wg_route(&b.dev, 0xC0A80164u) < 0,
          "route rejects foreign subnet");
}

static void test_rekey_replaces_session(void)
{
    node_t a, b;
    const uint8_t ts1[12] = { 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 };
    const uint8_t ts2[12] = { 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02 };
    uint8_t enc[64], dec[32];
    size_t enc_len, dec_len;
    int peer_idx = -1;

    node_setup(&a, "rekey-a-seed", 0x3333333333333333ull);
    node_setup(&b, "rekey-b-seed", 0x4444444444444444ull);
    (void)tsnode_wg_device_init(&a.dev, a.priv, tsnode_wg_crypto_host());
    (void)tsnode_wg_device_init(&b.dev, b.priv, tsnode_wg_crypto_host());
    link_nodes(&a, &b, 0x0A000101u, 0x0A000102u);

    handshake(&a, &b, ts1, 2000);
    (void)tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"old", 3,
                          2001, enc, sizeof(enc), &enc_len);

    /* Rehandshake with a newer timestamp: old session must be replaced
     * cleanly (v1 single-session semantics). */
    handshake(&a, &b, ts2, 3000);

    /* Old packet no longer decrypts (session rotated): auth must fail
     * closed instead of crashing or accepting. */
    tsnode_err_t err = tsnode_wg_decap(&b.dev, enc, enc_len, dec,
                                       sizeof(dec), &dec_len, &peer_idx);
    CHECK(err != TSNODE_OK, "stale-session packet rejected after rekey");

    /* New session still works both ways. */
    CHECK(tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"new", 3,
                          3001, enc, sizeof(enc), &enc_len) == TSNODE_OK,
          "encap under new session");
    CHECK(tsnode_wg_decap(&b.dev, enc, enc_len, dec, sizeof(dec), &dec_len,
                          &peer_idx) == TSNODE_OK &&
              memcmp(dec, "new", 3) == 0,
          "data flows under new session");
}

/* ---- 3. Hostile inputs ---- */

static void test_hostile_inputs(void)
{
    node_t a, b;
    const uint8_t ts[12] = { 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x05 };
    uint8_t init_pkt[TSNODE_WG_INITIATION_LEN];
    uint8_t resp_pkt[TSNODE_WG_RESPONSE_LEN];
    uint8_t enc[128], dec[96];
    size_t enc_len, dec_len;
    int peer_idx = -1;

    node_setup(&a, "hostile-a-seed", 0x5555555555555555ull);
    node_setup(&b, "hostile-b-seed", 0x6666666666666666ull);
    (void)tsnode_wg_device_init(&a.dev, a.priv, tsnode_wg_crypto_host());
    (void)tsnode_wg_device_init(&b.dev, b.priv, tsnode_wg_crypto_host());
    link_nodes(&a, &b, 0x0A000201u, 0x0A000202u);

    /* Truncated / oversized messages fail closed before any crypto. */
    (void)tsnode_wg_create_initiation(&a.dev, a.peer_idx, ts, init_pkt);
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, 147, &peer_idx) ==
              TSNODE_ERR_INVALID_ARG,
          "truncated initiation rejected");
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, 149, &peer_idx) ==
              TSNODE_ERR_INVALID_ARG,
          "oversized initiation rejected");
    init_pkt[0] = 99u; /* wrong message type (LE32 field) */
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, sizeof(init_pkt),
                                       &peer_idx) == TSNODE_ERR_INVALID_ARG,
          "wrong type on initiation rejected");

    /* Tampered ciphertext byte anywhere fails closed. */
    (void)tsnode_wg_create_initiation(&a.dev, a.peer_idx, ts, init_pkt);
    init_pkt[TEST_INIT_OFF_STATIC + 7] ^= 0x80u;
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, sizeof(init_pkt),
                                       &peer_idx) == TSNODE_ERR_CRYPTO,
          "tampered encrypted-static rejected");
    CHECK(b.dev.peers[b.peer_idx].hs_state == TSNODE_WG_HS_IDLE,
          "failed initiation leaves responder idle");
    (void)tsnode_wg_create_initiation(&a.dev, a.peer_idx, ts, init_pkt);
    init_pkt[130] ^= 0x01u; /* inside mac2 region */
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, sizeof(init_pkt),
                                       &peer_idx) == TSNODE_ERR_CRYPTO,
          "tampered mac region rejected");

    /* Stale timestamp replay of the same initiation. */
    uint8_t replay_pkt[TSNODE_WG_INITIATION_LEN];
    memcpy(replay_pkt, init_pkt, sizeof(init_pkt));
    /* Use a NEWER timestamp for the clean initiation (anti-replay
     * requires strictly increasing). */
    const uint8_t ts2[12] = { 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x06 };
    (void)tsnode_wg_create_initiation(&a.dev, a.peer_idx, ts2, init_pkt);
    CHECK(tsnode_wg_consume_initiation(&b.dev, init_pkt, sizeof(init_pkt),
                                       &peer_idx) == TSNODE_OK,
          "clean initiation accepted");
    (void)tsnode_wg_create_response(&b.dev, peer_idx, 4000, resp_pkt);
    CHECK(tsnode_wg_consume_response(&a.dev, resp_pkt, sizeof(resp_pkt),
                                     4000, &peer_idx) == TSNODE_OK,
          "handshake completes");
    /* Exact same initiation replayed: timestamp check must reject. */
    CHECK(tsnode_wg_consume_initiation(&b.dev, replay_pkt,
                                       sizeof(replay_pkt),
                                       &peer_idx) == TSNODE_ERR_CRYPTO,
          "replayed initiation rejected (timestamp anti-replay)");

    /* Transport-level hostility. */
    CHECK(tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"secret", 6,
                          5000, enc, sizeof(enc), &enc_len) == TSNODE_OK,
          "encap for hostile cases");
    enc[20] ^= 0xFFu; /* flip a ciphertext bit */
    CHECK(tsnode_wg_decap(&b.dev, enc, enc_len, dec, sizeof(dec), &dec_len,
                          &peer_idx) != TSNODE_OK,
          "forged transport packet rejected");
    CHECK(b.dev.peers[b.peer_idx].session.valid,
          "forged packet does not kill session");

    /* Replay the untouched original twice: second copy must hit the
     * replay filter (first was never sent — send another to consume). */
    uint8_t orig[128];
    size_t orig_len;
    (void)tsnode_wg_encap(&a.dev, a.peer_idx, (const uint8_t *)"orig", 4,
                          5001, orig, sizeof(orig), &orig_len);
    CHECK(tsnode_wg_decap(&b.dev, orig, orig_len, dec, sizeof(dec),
                          &dec_len, &peer_idx) == TSNODE_OK,
          "original accepted once");
    CHECK(tsnode_wg_decap(&b.dev, orig, orig_len, dec, sizeof(dec),
                          &dec_len, &peer_idx) == TSNODE_ERR_REPLAY,
          "duplicate returns TSNODE_ERR_REPLAY");
    CHECK(tsnode_wg_decap(&b.dev, orig, orig_len, dec, sizeof(dec),
                          &dec_len, &peer_idx) == TSNODE_ERR_REPLAY,
          "triple duplicate also flagged as replay");

    /* Unknown receiver index. */
    orig[4] ^= 0xFFu;
    orig[5] ^= 0xFFu;
    orig[6] ^= 0xFFu;
    orig[7] ^= 0xFFu;
    CHECK(tsnode_wg_decap(&b.dev, orig, orig_len, dec, sizeof(dec),
                          &dec_len, &peer_idx) == TSNODE_ERR_CRYPTO,
          "unknown session index dropped");

    /* Response consumed without pending handshake. */
    (void)tsnode_wg_create_initiation(&a.dev, a.peer_idx, ts, init_pkt);
    CHECK(tsnode_wg_consume_response(&b.dev, resp_pkt, sizeof(resp_pkt),
                                     6000, &peer_idx) == TSNODE_ERR_CRYPTO,
          "response without staged handshake rejected");

    /* Encap without session. */
    node_t lonely;
    node_setup(&lonely, "lonely-seed", 0x7777777777777777ull);
    (void)tsnode_wg_device_init(&lonely.dev, lonely.priv,
                                tsnode_wg_crypto_host());
    CHECK(tsnode_wg_encap(&lonely.dev, 0, (const uint8_t *)"z", 1, 7000, enc,
                          sizeof(enc), &enc_len) == TSNODE_ERR_INVALID_STATE,
          "encap without session fails closed");
}

int main(void)
{
    test_x25519_vector();
    test_chacha20_block_vector();
    test_aead_vector();
    test_handshake_roundtrip_and_data();
    test_rekey_replaces_session();
    test_hostile_inputs();

    if (failures > 0) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nall wg core tests passed\n");
    return 0;
}
