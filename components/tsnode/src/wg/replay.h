/*
 * Anti-replay filter for WireGuard transport data (ADR-0008 D1.4,
 * AGENTS.md §2.2).
 *
 * Faithful C11 port of WireGuard's production anti-replay algorithm
 * (RFC 6479-style sliding window), attributed per AGENTS.md §6:
 *
 *   Source:  WireGuard/wireguard-go, replay/replay.go @ master
 *            (fetched 2026-08-24), SPDX MIT, (C) WireGuard LLC.
 *   Why:     audited, deployed algorithm identical to what real peers
 *            run; reimplementing a bitmap scheme from scratch was tried
 *            first and produced subtle ring-aliasing bugs caught by the
 *            differential test in tests/unit/test_replay.c.
 *
 * Divergence note vs the whitepaper's nominal 2048-bit window: this
 * implementation keeps an internal ring of 128 words with an effective
 * window of (128-1)*64 = 8064 counters, exactly like every real
 * WireGuard peer (wireguard-go/kernel). Accepting within a wider window
 * never breaks interop; it only bounds how far back a replayed packet
 * may still be detected instead of dropped-by-window.
 *
 * Transport counters START AT 0 (verified against device/send.go:
 * nonce = sendNonce.Add(1) - 1): unlike ts2021 record nonces, zero is a
 * valid first sequence number here.
 *
 * Single-threaded ownership per ADR-0008 D3: no internal locking.
 * Pure C11, no platform headers (ADR-0006).
 */

#ifndef TSNODE_WG_REPLAY_H
#define TSNODE_WG_REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSNODE_WG_REPLAY_BLOCK_BIT_LOG 6u /* 1<<6 == 64 bits */
#define TSNODE_WG_REPLAY_BLOCK_BITS 64u   /* must be power of two */
#define TSNODE_WG_REPLAY_RING_BLOCKS 128u /* must be power of two */
#define TSNODE_WG_REPLAY_WINDOW \
    ((uint64_t)(TSNODE_WG_REPLAY_RING_BLOCKS - 1u) * \
     TSNODE_WG_REPLAY_BLOCK_BITS)

typedef struct {
    uint64_t last;
    uint64_t ring[TSNODE_WG_REPLAY_RING_BLOCKS];
} tsnode_wg_replay_t;

/* Resets the filter to its initial (empty) state. */
void tsnode_wg_replay_init(tsnode_wg_replay_t *r);

/*
 * Checks whether transport counter `counter` should be accepted and marks
 * it as seen on success. Returns true iff the caller MUST process the
 * packet. Counters >= limit are always rejected (WireGuard
 * RejectAfterMessages bound is passed by the caller).
 */
bool tsnode_wg_replay_check_and_update(tsnode_wg_replay_t *r,
                                       uint64_t counter, uint64_t limit);

#ifdef __cplusplus
}
#endif

#endif /* TSNODE_WG_REPLAY_H */
