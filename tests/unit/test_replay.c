/*
 * Anti-replay filter host tests.
 *
 * Contract verified against wireguard-go semantics (see replay.h):
 * counters start at 0, window = 8064, limit bound passed by caller.
 *
 * Layers: targeted edges + differential vs a naive model under a
 * deterministic pseudo-random op stream (no libc rand).
 */

#include <stdio.h>
#include <string.h>

#include "replay.h"

#define LIMIT UINT64_MAX

static int failures;

/* ---- naive reference model ---- */

#define MODEL_CAP 16384

typedef struct {
    uint64_t last;
    uint64_t seen[MODEL_CAP]; /* ring of recently accepted counters */
    size_t seen_n;
} naive_t;

static void naive_init(naive_t *m)
{
    m->last = 0;
    m->seen_n = 0;
}

static bool naive_accept(naive_t *m, uint64_t seq)
{
    if (seq >= LIMIT) {
        return false;
    }
    if (seq <= m->last && m->last - seq > TSNODE_WG_REPLAY_WINDOW) {
        return false;
    }
    for (size_t i = 0; i < m->seen_n; i++) {
        if (m->seen[i] == seq) {
            return false;
        }
    }
    if (seq > m->last) {
        m->last = seq;
    }
    m->seen[m->seen_n % MODEL_CAP] = seq;
    m->seen_n++;
    return true;
}

static uint64_t lcg_state = 0x243F6A8885A308D3ull;

static uint64_t lcg_next(void)
{
    lcg_state = lcg_state * 6364136223846793005ull +
                1442695040888963407ull;
    return lcg_state >> 11;
}

static void expect(bool cond, const char *name)
{
    if (!cond) {
        printf("FAIL %s\n", name);
        failures++;
    } else {
        printf("PASS %s\n", name);
    }
}

static void test_basics(void)
{
    tsnode_wg_replay_t r;
    tsnode_wg_replay_init(&r);

    printf("replay basics:\n");
    expect(tsnode_wg_replay_check_and_update(&r, 0, LIMIT),
           "counter 0 accepted (first transport packet)");
    expect(!tsnode_wg_replay_check_and_update(&r, 0, LIMIT),
           "counter 0 replay rejected");
    expect(tsnode_wg_replay_check_and_update(&r, 1, LIMIT), "counter 1 accepted");

    bool ok = true;
    for (uint64_t s = 2; s <= 100; s++) {
        ok = ok && tsnode_wg_replay_check_and_update(&r, s, LIMIT);
    }
    expect(ok, "sequential 2..100 accepted");
    expect(!tsnode_wg_replay_check_and_update(&r, 50, LIMIT),
           "immediate replay rejected");
}

static void test_window_edges(void)
{
    tsnode_wg_replay_t r;
    tsnode_wg_replay_init(&r);

    /* Warm up marking everything except one hole inside the window. */
    for (uint64_t s = 0; s < TSNODE_WG_REPLAY_WINDOW; s++) {
        if (s == 1000) {
            continue;
        }
        if (!tsnode_wg_replay_check_and_update(&r, s, LIMIT)) {
            printf("FAIL warmup at %llu\n", (unsigned long long)s);
            failures++;
            return;
        }
    }

    printf("replay window edges:\n");
    expect(tsnode_wg_replay_check_and_update(&r, 1000, LIMIT),
           "in-window hole accepted");
    expect(!tsnode_wg_replay_check_and_update(&r, 1000, LIMIT),
           "in-window hole replay rejected");

    /* Boundary via forward jump: bottom of new window acceptable iff
     * never seen; exactly one below it is out of window forever. */
    uint64_t f = 20000;
    expect(tsnode_wg_replay_check_and_update(&r, f, LIMIT),
           "jump target accepted");
    expect(tsnode_wg_replay_check_and_update(&r, f - TSNODE_WG_REPLAY_WINDOW,
                                             LIMIT),
           "delta == WINDOW bottom accepted");
    expect(!tsnode_wg_replay_check_and_update(
               &r, f - TSNODE_WG_REPLAY_WINDOW - 1, LIMIT),
           "delta == WINDOW+1 rejected");
}

static void test_big_jump(void)
{
    tsnode_wg_replay_t r;
    tsnode_wg_replay_init(&r);

    for (uint64_t s = 0; s <= 10; s++) {
        (void)tsnode_wg_replay_check_and_update(&r, s, LIMIT);
    }
    printf("replay big jumps:\n");

    uint64_t far = 1000000000ull;
    expect(tsnode_wg_replay_check_and_update(&r, far, LIMIT),
           "huge jump accepted");
    expect(tsnode_wg_replay_check_and_update(
               &r, far - TSNODE_WG_REPLAY_WINDOW, LIMIT),
           "bottom of new window accepted");
    expect(!tsnode_wg_replay_check_and_update(
               &r, far - TSNODE_WG_REPLAY_WINDOW - 1, LIMIT),
           "just below new window rejected");
    expect(!tsnode_wg_replay_check_and_update(&r, 5, LIMIT),
           "pre-jump counter rejected");

    /* Window-sized jumps repeatedly must stay consistent. */
    tsnode_wg_replay_init(&r);
    uint64_t top = 0;
    (void)tsnode_wg_replay_check_and_update(&r, top, LIMIT);
    bool ok = true;
    for (int i = 0; i < 50; i++) {
        top += TSNODE_WG_REPLAY_WINDOW;
        ok = ok && tsnode_wg_replay_check_and_update(&r, top, LIMIT);
        ok = ok && !tsnode_wg_replay_check_and_update(
                       &r, top - TSNODE_WG_REPLAY_WINDOW, LIMIT);
        ok = ok && !tsnode_wg_replay_check_and_update(&r, top, LIMIT);
    }
    expect(ok, "window-sized jumps behave");
}

static void test_limit_bound(void)
{
    tsnode_wg_replay_t r;
    tsnode_wg_replay_init(&r);

    printf("replay limit bound:\n");
    const uint64_t reject_after = (1ull << 63); /* arbitrary small-ish */
    expect(!tsnode_wg_replay_check_and_update(&r, reject_after, reject_after),
           "counter == limit rejected");
    expect(!tsnode_wg_replay_check_and_update(&r, reject_after + 1,
                                              reject_after),
           "counter > limit rejected");
    expect(tsnode_wg_replay_check_and_update(&r, reject_after - 1,
                                             reject_after),
           "counter just below limit accepted");
}

static void test_counter_exhaustion(void)
{
    tsnode_wg_replay_t r;
    tsnode_wg_replay_init(&r);

    printf("replay near counter exhaustion:\n");
    /* With LIMIT == UINT64_MAX the filter itself accepts up to max-1;
     * in production wg.c passes RejectAfterMessages well below this,
     * mirroring wireguard-go's ValidateCounter(counter, limit) call. */
    expect(tsnode_wg_replay_check_and_update(&r, UINT64_MAX - 2, LIMIT),
           "UINT64_MAX-2 accepted");
    expect(!tsnode_wg_replay_check_and_update(&r, UINT64_MAX - 2, LIMIT),
           "UINT64_MAX-2 replay rejected");
    expect(!tsnode_wg_replay_check_and_update(&r, UINT64_MAX, LIMIT),
           "UINT64_MAX rejected at limit boundary");
    expect(!tsnode_wg_replay_check_and_update(&r, 12345, LIMIT),
           "post-max small counter rejected");
}

/* ---- differential vs naive model ---- */

static void test_differential(void)
{
    tsnode_wg_replay_t r;
    naive_t n;
    tsnode_wg_replay_init(&r);
    naive_init(&n);

    enum { OPS = 20000 };
    uint64_t top = 0;
    uint64_t recent[256];
    size_t recent_n = 0;
    int mismatches = 0;

    for (int i = 0; i < OPS; i++) {
        uint64_t seq;
        uint64_t roll = lcg_next() % 10;

        if (roll < 5) {
            seq = top + 1; /* sequential (includes first packet at 0) */
        } else if (roll < 7 && recent_n > 0) {
            seq = recent[lcg_next() % recent_n]; /* replay */
        } else if (roll < 8) {
            seq = top - (lcg_next() % (2 * TSNODE_WG_REPLAY_WINDOW));
            /* old / around the window edge */
        } else if (roll < 9) {
            static const uint64_t deltas[] = {
                TSNODE_WG_REPLAY_WINDOW - 1,
                TSNODE_WG_REPLAY_WINDOW,
                TSNODE_WG_REPLAY_WINDOW + 1,
                TSNODE_WG_REPLAY_RING_BLOCKS *64u - 1u, /* ring-width jump */
                TSNODE_WG_REPLAY_RING_BLOCKS *64u,
                TSNODE_WG_REPLAY_RING_BLOCKS *64u + 1u,
            };
            seq = top + 1 + deltas[lcg_next() % 6];
        } else {
            seq = top + 1 + (lcg_next() % 20000); /* generic forward jump */
        }

        bool got = tsnode_wg_replay_check_and_update(&r, seq, LIMIT);
        bool want = naive_accept(&n, seq);
        if (got != want) {
            if (mismatches < 5) {
                printf("FAIL diff op %d: seq=%llu impl=%d model=%d\n", i,
                       (unsigned long long)seq, got ? 1 : 0, want ? 1 : 0);
            }
            mismatches++;
            break;
        }
        if (got) {
            recent[recent_n % 256] = seq;
            recent_n++;
        }
        if (seq > top) {
            top = seq;
        }
    }

    if (mismatches == 0) {
        printf("PASS differential (%d ops)\n", OPS);
    } else {
        printf("FAIL differential (%d mismatches)\n", mismatches);
        failures++;
    }
}

int main(void)
{
    test_basics();
    test_window_edges();
    test_big_jump();
    test_limit_bound();
    test_counter_exhaustion();
    test_differential();

    if (failures) {
        printf("\nREPLAY TESTS: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nREPLAY TESTS: ALL PASS\n");
    return 0;
}
