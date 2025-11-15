/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef C4_ETH_PERIOD_STORE_H
#define C4_ETH_PERIOD_STORE_H

#include "server.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Called on each new head to persist block root and 112-byte header at the slot position.
 * Safe against reorgs by overwriting the slot index within the current period.
 *
 * @param slot       Beacon slot number
 * @param block_root 32-byte root
 * @param header112  112-byte serialized header (slot, proposer_index, parent_root, state_root, body_root)
 */
void c4_period_sync_on_head(uint64_t slot, const uint8_t block_root[32], const uint8_t header112[112]);

#ifdef __cplusplus
}
#endif

#endif /* C4_ETH_PERIOD_STORE_H */
