/*
 * Anti-replay filter — see replay.h for the algorithm's provenance,
 * divergence notes and threading contract. The logic below mirrors
 * wireguard-go replay.Filter.ValidateCounter line by line; keep it in
 * sync with that reference when updating.
 */

#include "replay.h"

#include <string.h>

#define BLOCK_MASK (TSNODE_WG_REPLAY_RING_BLOCKS - 1u)
#define BIT_MASK (TSNODE_WG_REPLAY_BLOCK_BITS - 1u)

void tsnode_wg_replay_init(tsnode_wg_replay_t *r)
{
    if (r == NULL) {
        return;
    }
    r->last = 0;
    memset(r->ring, 0, sizeof(r->ring));
}

bool tsnode_wg_replay_check_and_update(tsnode_wg_replay_t *r,
                                       uint64_t counter, uint64_t limit)
{
    if (r == NULL || counter >= limit) {
        return false;
    }

    uint64_t index_block = counter >> TSNODE_WG_REPLAY_BLOCK_BIT_LOG;

    if (counter > r->last) {
        /* Move window forward: clear every word between the old top word
         * (exclusive) and the new one (inclusive), capped to the ring size
         * so a huge jump wipes the whole ring. Word indices are absolute;
         * masking maps them into ring slots without aliasing because the
         * effective window is one word narrower than the ring. */
        uint64_t current = r->last >> TSNODE_WG_REPLAY_BLOCK_BIT_LOG;
        uint64_t diff = index_block - current;
        if (diff > TSNODE_WG_REPLAY_RING_BLOCKS) {
            diff = TSNODE_WG_REPLAY_RING_BLOCKS;
        }
        for (uint64_t i = current + 1u; i <= current + diff; i++) {
            r->ring[i & BLOCK_MASK] = 0;
        }
        r->last = counter;
    } else if (r->last - counter > TSNODE_WG_REPLAY_WINDOW) {
        /* Behind the current window: unconditionally reject. */
        return false;
    }

    /* Check-and-set the bit. Single set of a fresh bit => accept. */
    uint64_t slot = index_block & BLOCK_MASK;
    uint64_t bit = (uint64_t)1 << (counter & BIT_MASK);
    uint64_t old = r->ring[slot];
    r->ring[slot] = old | bit;
    return r->ring[slot] != old;
}
