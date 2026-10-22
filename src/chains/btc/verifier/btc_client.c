/*
 * Copyright (c) 2025 corpus.core
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

#include "btc_client.h"
#include "btc_hash.h"
#include "btc_target.h"
#include "bytes.h"
#include "plugin.h"
#include <string.h>

/**
 * Builds the storage key shared with `c4_get_client_state`.
 *
 * @param chain_id chain identifier
 * @param key receives the key
 * @param key_len size of `key`
 * @return false when the key does not fit
 */
static bool client_state_key(chain_id_t chain_id, char* key, size_t key_len) {
  char local[80];
  sbprintf(local, "states_%l", (uint64_t) chain_id);
  if (!local[0] || strlen(local) + 1 > key_len || !c4_storage_name_is_safe(local)) return false;
  memcpy(key, local, strlen(local) + 1);
  return true;
}

/**
 * Reads a little-endian uint32.
 *
 * @param p four bytes
 * @return the integer
 */
static uint32_t client_read_le32(const uint8_t p[4]) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/**
 * Writes a little-endian uint32.
 *
 * @param p four-byte destination
 * @param value integer to write
 */
static void client_write_le32(uint8_t p[4], uint32_t value) {
  p[0] = (uint8_t) value;
  p[1] = (uint8_t) (value >> 8);
  p[2] = (uint8_t) (value >> 16);
  p[3] = (uint8_t) (value >> 24);
}

void btc_client_state_set_checkpoint(btc_client_state_t* state) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->height = BTC_MAINNET_CHECKPOINT_HEIGHT;
  btc_header_check(btc_mainnet_checkpoint_header(), state->hash);
}

bool btc_client_state_encode(const btc_client_state_t* state, buffer_t* out) {
  uint8_t raw[BTC_CLIENT_STATE_SIZE];
  if (!state || !out) return false;
  raw[0] = BTC_CLIENT_STATE_TAG;
  client_write_le32(raw + 1, state->height);
  memcpy(raw + 5, state->hash, 32);
  buffer_reset(out);
  return buffer_append(out, bytes(raw, BTC_CLIENT_STATE_SIZE)) == BTC_CLIENT_STATE_SIZE;
}

bool btc_client_state_decode(bytes_t blob, btc_client_state_t* state) {
  btc_client_state_t parsed;
  uint8_t            checkpoint[32];
  if (!state || !blob.data || blob.len != BTC_CLIENT_STATE_SIZE) return false;
  if (blob.data[0] != BTC_CLIENT_STATE_TAG) return false;
  memset(&parsed, 0, sizeof(parsed));
  parsed.height = client_read_le32(blob.data + 1);
  memcpy(parsed.hash, blob.data + 5, 32);
  if (parsed.height == BTC_MAINNET_CHECKPOINT_HEIGHT) {
    if (!btc_header_check(btc_mainnet_checkpoint_header(), checkpoint)) return false;
    if (memcmp(parsed.hash, checkpoint, 32) != 0) return false;
  }
  *state = parsed;
  return true;
}

bool btc_client_state_load(chain_id_t chain_id, btc_client_state_t* state) {
  storage_plugin_t storage = {0};
  buffer_t         blob    = {0};
  char             key[80];
  bool             ok = false;
  if (!state) return false;
  btc_client_state_set_checkpoint(state);
  if (!client_state_key(chain_id, key, sizeof(key))) return false;
  c4_get_storage_config(&storage);
  if (!storage.get || !storage.get(key, &blob) || !blob.data.data || blob.data.len == 0) {
    buffer_free(&blob);
    return false;
  }
  ok = btc_client_state_decode(blob.data, state);
  buffer_free(&blob);
  if (!ok) btc_client_state_set_checkpoint(state);
  return ok;
}

bool btc_client_state_store(chain_id_t chain_id, const btc_client_state_t* state) {
  storage_plugin_t storage = {0};
  buffer_t         blob    = {0};
  char             key[80];
  if (!state || !client_state_key(chain_id, key, sizeof(key))) return false;
  c4_get_storage_config(&storage);
  if (!storage.set || !btc_client_state_encode(state, &blob)) {
    buffer_free(&blob);
    return false;
  }
  storage.set(key, blob.data);
  buffer_free(&blob);
  return true;
}
