/*
 * Copyright (c) 2025,2026 corpus.core
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

#include "prover_cache_url.h"
#include "beacon_types.h"
#include "bytes.h"
#include <string.h>

char* c4_eth_build_delegated_block_get_url(const char* method, json_t block, uint32_t version,
                                           bool zk_proof, bytes_t client_state, bytes_t witness_key) {
  buffer_t url = {0};
  // `%j` emits the raw block identifier without surrounding quotes (e.g. `latest`, `0x1234`),
  // which is URL-safe for tags and hex values alike. Any extra RPC arguments (e.g. `includeTx`
  // for `eth_getBlockByNumber`) are intentionally omitted because they do not affect the proof.
  bprintf(&url, "proof/%s/%j/%d/%s/", method, block, version, zk_proof ? "zk" : "std");
  if (client_state.data && client_state.len)
    bprintf(&url, "0x%x", client_state);
  else
    bprintf(&url, "0x");
  // Signer/witness keys go into a query parameter: the proof stays deterministic per signer set
  // and therefore cacheable, while keeping the (large) key list out of the cache-key path segments.
  if (witness_key.data && witness_key.len)
    bprintf(&url, "?signers=0x%x", witness_key);
  return buffer_as_string(url);
}

uint32_t c4_eth_block_ttl_s(chain_id_t chain_id, json_t block, bool light_client) {
  // Concrete block numbers/hashes are immutable and do not need a client-side freshness bound.
  // Only textual tags map to a bounded max-age (mirroring the prover-side header cache TTL).
  const chain_spec_t* spec       = c4_eth_get_chain_spec(chain_id);
  uint64_t            block_time = is_gnosis_chain(chain_id) ? 5 : 12;
  uint64_t            spe        = 1ULL << (spec ? spec->slots_per_epoch_bits : 5);

  if (block.type != JSON_TYPE_STRING || !block.start) return 0;
  if (strncmp(block.start, "\"latest\"", 8) == 0)
    return (uint32_t) (light_client ? block_time : block_time / 2);
  if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"justified\"", 11) == 0)
    return (uint32_t) ((spe / 2) * block_time);
  if (strncmp(block.start, "\"finalized\"", 11) == 0)
    return (uint32_t) (spe * block_time);
  return 0;
}

// Whitelist for the block identifier that ends up as a raw path segment in the GET URL. Only
// well-known tags and even-length 0x-prefixed hex tokens are accepted. Any other content
// (including `pending`, `earliest`, `head`, or anything containing `/`, `?`, `#`, `%`, whitespace,
// CR/LF) causes the dispatcher to fall back to the classic POST payload path, so the URL
// structure can never be broken by caller-supplied input.
static bool is_safe_block_token(json_t b) {
  if (b.type != JSON_TYPE_STRING || b.len < 3 || !b.start) return false;
  const char* s = b.start + 1; // strip opening quote
  uint32_t    n = b.len - 2;   // strip surrounding quotes
  if (n == 6 && memcmp(s, "latest", 6) == 0) return true;
  if (n == 4 && memcmp(s, "safe", 4) == 0) return true;
  if (n == 9 && memcmp(s, "justified", 9) == 0) return true;
  if (n == 9 && memcmp(s, "finalized", 9) == 0) return true;
  // 0x-prefixed hex: at least one nibble, only [0-9a-fA-F]. Odd lengths are legal because JSON-RPC
  // block numbers are minimally-encoded (e.g. `0x1b4` == 436, not `0x01b4`). Block hashes are
  // always 32 bytes / 64 hex chars, block numbers are up to 16 nibbles, so cap the token at 66
  // chars including the `0x` prefix.
  if (n < 3 || n > 66) return false;
  if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
  for (uint32_t i = 2; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

bool c4_eth_get_prover_cache_request(chain_id_t chain_id, const char* method, json_t params,
                                     uint32_t version, bool zk_proof, bool light_client,
                                     bytes_t client_state, bytes_t witness_key,
                                     char** url_out, uint32_t* ttl_out) {
  if (!method || !url_out || !ttl_out) return false;
  // Only methods whose proof is a pure function of (block, version, zk, client_state, witness_key)
  // are eligible for CDN caching. Everything else (including the tx-hash-based PAP fallback)
  // must stay POST because either the payload leaks caller intent or it depends on runtime state.
  bool eligible = strcmp(method, "eth_getBlockHeader") == 0 ||
                  strcmp(method, "eth_getBlockByNumber") == 0 ||
                  strcmp(method, "eth_getBlockReceipts") == 0;
  if (!eligible) return false;

  json_t block = json_at(params, 0);
  if (!is_safe_block_token(block)) return false;

  char* url = c4_eth_build_delegated_block_get_url(method, block, version, zk_proof, client_state, witness_key);
  if (!url) return false;

  // Ownership of the freshly allocated URL transfers to the caller (typically stored in
  // `data_request_t.url` and released via `c4_request_free`).
  *url_out = url;
  *ttl_out = c4_eth_block_ttl_s(chain_id, block, light_client);
  return true;
}
