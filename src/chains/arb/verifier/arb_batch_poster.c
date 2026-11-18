/*
 * Copyright (c) 2025,2026 corpus.core GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "arb_batch_poster.h"
#include "bytes.h"
#include "crypto.h"
#include "plugin.h"
#include <string.h>

static const uint8_t ARB_ONE_SEQUENCER_INBOX[20] = {
    0x1c, 0x47, 0x96, 0x75, 0xad, 0x55, 0x9d, 0xc1, 0x51, 0xf6,
    0xec, 0x7e, 0xd3, 0xfb, 0xf8, 0xce, 0xe7, 0x95, 0x82, 0xb6};

bool arb_sequencer_inbox(chain_id_t chain_id, uint8_t address[20]) {
  if (!address || chain_id != C4_CHAIN_ARB_ONE) return false;
  memcpy(address, ARB_ONE_SEQUENCER_INBOX, 20);
  return true;
}

void arb_batch_poster_storage_key(const uint8_t poster[20], uint8_t out[32]) {
  uint8_t preimage[64] = {0};
  if (!poster || !out) return;
  memcpy(preimage + 12, poster, 20);
  preimage[63] = (uint8_t) ARB_IS_BATCH_POSTER_SLOT;
  keccak(bytes(preimage, 64), out);
}

bool arb_batch_poster_l1_is_current(chain_id_t chain_id, uint64_t l1_number) {
  uint8_t  address[20] = {0};
  uint64_t timestamp   = 0;
  uint64_t stored      = 0;
  if (!c4_get_cached_batch_poster(chain_id, address, &timestamp, &stored)) return true;
  return l1_number >= stored;
}
