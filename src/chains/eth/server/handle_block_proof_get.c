/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon_types.h"
#include "handler.h"
#include "logger.h"
#include "util/bytes.h"
#include "util/chain_props.h"
#include "util/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROOF_GET_PREFIX      "/proof/"
#define PROOF_GET_MAX_PATH    512  /**< max length of the path portion (before '?') */
#define PROOF_GET_MAX_C4_HEX  512  /**< max hex length of the c4 client_state segment */
#define PROOF_GET_MAX_SIGNERS 4096 /**< max hex length of the signers query value */

// Sends a JSON error response for the cache-friendly GET endpoint with `Cache-Control: no-store`.
// Since the GET URLs are deterministic, plain error responses could otherwise be cached by a CDN.
static void proof_get_error(client_t* client, int status, const char* msg) {
  buffer_t body = {0};
  bprintf(&body, "{\"error\":\"%S\"}", msg);
  char* hdr = "Cache-Control: no-store\r\n";
  c4_http_respond_ex(client, status, "application/json", body.data, bytes((uint8_t*) hdr, (uint32_t) strlen(hdr)));
  buffer_free(&body);
}

// Returns true if the block identifier is safe to embed into the JSON params array. Accepts only
// alphanumeric characters [0-9a-zA-Z], which covers both textual tags (`latest`, `safe`,
// `finalized`, ...) and `0x`-prefixed hex block numbers/hashes, while rejecting quotes,
// backslashes, brackets and control characters that could break out of the JSON string.
static bool is_safe_block_id(const char* s) {
  if (!s || !*s) return false;
  for (; *s; s++) {
    char c = *s;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;
}

// Decodes a "0x"-prefixed, even-length hex string into a freshly allocated bytes_t.
// Returns NULL_BYTES on any malformed input (caller must free `.data` on success).
static bytes_t decode_hex_0x(const char* s, uint32_t max_hex_len) {
  if (!s || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return NULL_BYTES;
  const char* hex    = s + 2;
  uint32_t    hexlen = 0;
  while (hex[hexlen]) {
    char c = hex[hexlen];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return NULL_BYTES;
    if (++hexlen > max_hex_len) return NULL_BYTES;
  }
  if (hexlen == 0 || (hexlen % 2)) return NULL_BYTES;
  uint32_t blen = hexlen / 2;
  uint8_t* data = safe_malloc(blen);
  if (hex_to_bytes(hex, (int) hexlen, bytes(data, blen)) != (int) blen) {
    safe_free(data);
    return NULL_BYTES;
  }
  return bytes(data, blen);
}

// Extracts the `signers=0x...` value from the query string into a heap bytes_t (or NULL_BYTES).
static bytes_t parse_signers_query(const char* query) {
  if (!query) return NULL_BYTES;
  const char* found = strstr(query, "signers=");
  if (!found) return NULL_BYTES;
  found += strlen("signers=");
  char     tmp[PROOF_GET_MAX_SIGNERS + 3] = {0};
  uint32_t i                              = 0;
  for (; found[i] && found[i] != '&' && i < sizeof(tmp) - 1; i++) tmp[i] = found[i];
  if (found[i] && found[i] != '&') return NULL_BYTES; // value exceeds the bound
  return decode_hex_0x(tmp, PROOF_GET_MAX_SIGNERS);
}

// Builds the `Cache-Control` header value for a block identifier, mirroring the client-side TTL
// logic (`header_tag_ttl_ms`): concrete block numbers/hashes are immutable, tags get a bounded TTL.
// Non-static so it can be unit-tested directly.
void c4_eth_block_cache_control(char* out, size_t cap, const char* block, chain_id_t chain_id) {
  const chain_spec_t* spec       = c4_eth_get_chain_spec(chain_id);
  chain_properties_t  props      = {0};
  uint32_t            block_time = c4_chains_get_props(chain_id, &props) ? (uint32_t) (props.block_time / 1000) : 12;
  uint32_t            spe        = 1u << (spec ? spec->slots_per_epoch_bits : 5);

  if (strcmp(block, "latest") == 0)
    snprintf(out, cap, "public, max-age=%u, stale-while-revalidate=%u", block_time / 2, block_time / 2);
  else if (strcmp(block, "safe") == 0 || strcmp(block, "justified") == 0)
    snprintf(out, cap, "public, max-age=%u", (spe / 2) * block_time);
  else if (strcmp(block, "finalized") == 0)
    snprintf(out, cap, "public, max-age=%u", spe * block_time);
  else if (block[0] == '0' && (block[1] == 'x' || block[1] == 'X'))
    // Concrete block number/hash (and genesis): the proof for a given hash/number is immutable.
    snprintf(out, cap, "public, max-age=31536000, immutable");
  else
    // Any other alphanumeric token (e.g. an unknown/future tag): the prover will reject it with
    // an error, but out of caution mark the response as non-cacheable so a shared CDN can never
    // pin a bad answer for a year.
    snprintf(out, cap, "no-store");
}

/**
 * Handles cache-friendly GET proof requests for delegated block methods (hybrid mode):
 *   `GET /proof/<method>/<block>/<version>/<zk|std>/<c4>[?signers=0x..]`
 *
 * Reconstructs the equivalent proof request and dispatches it through the shared
 * `c4_proof_request_dispatch`, attaching a block-tag-dependent `Cache-Control` header.
 */
bool c4_handle_proof_get_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_GET) return false;
  if (strncmp(client->request.path, PROOF_GET_PREFIX, strlen(PROOF_GET_PREFIX)) != 0) return false;

  // Separate the path from an optional query string.
  const char* full  = client->request.path;
  const char* qmark = strchr(full, '?');
  size_t      plen  = qmark ? (size_t) (qmark - full) : strlen(full);
  if (plen >= PROOF_GET_MAX_PATH) {
    proof_get_error(client, 400, "Proof GET path too long");
    return true;
  }

  char pathbuf[PROOF_GET_MAX_PATH];
  memcpy(pathbuf, full, plen);
  pathbuf[plen] = '\0';

  // Split the segments after the "/proof/" prefix.
  char* rest      = pathbuf + strlen(PROOF_GET_PREFIX);
  char* save      = NULL;
  char* s_method  = c4_strtok_r(rest, "/", &save);
  char* s_block   = c4_strtok_r(NULL, "/", &save);
  char* s_version = c4_strtok_r(NULL, "/", &save);
  char* s_zk      = c4_strtok_r(NULL, "/", &save);
  char* s_c4      = c4_strtok_r(NULL, "/", &save);
  char* s_extra   = c4_strtok_r(NULL, "/", &save);
  if (!s_method || !s_block || !s_version || !s_zk || !s_c4 || s_extra) {
    proof_get_error(client, 400, "Invalid proof GET path (expected /proof/<method>/<block>/<version>/<zk|std>/<c4>)");
    return true;
  }

  bool is_header   = strcmp(s_method, "eth_getBlockHeader") == 0;
  bool is_block    = strcmp(s_method, "eth_getBlockByNumber") == 0;
  bool is_receipts = strcmp(s_method, "eth_getBlockReceipts") == 0;
  if (!is_header && !is_block && !is_receipts) {
    proof_get_error(client, 400, "Unsupported method for proof GET");
    return true;
  }
  if (!is_safe_block_id(s_block)) {
    proof_get_error(client, 400, "Invalid block identifier");
    return true;
  }

  // version segment: strict decimal (reject leading sign/whitespace that strtoul would accept)
  char*         end   = NULL;
  unsigned long vlong = strtoul(s_version, &end, 10);
  if (s_version[0] < '0' || s_version[0] > '9' || !end || *end != '\0' || vlong > 0xffffffffUL) {
    proof_get_error(client, 400, "Invalid version");
    return true;
  }
  uint32_t version_num = (uint32_t) vlong;

  // zk segment
  prover_flags_t extra_flags = 0;
  if (strcmp(s_zk, "zk") == 0)
    extra_flags |= C4_PROVER_FLAG_ZK_PROOF;
  else if (strcmp(s_zk, "std") != 0) {
    proof_get_error(client, 400, "Invalid zk segment (expected 'zk' or 'std')");
    return true;
  }

  // c4 segment: "0x" means an empty client_state, otherwise a 0x-hex snapshot.
  bytes_t cs = NULL_BYTES;
  if (!(s_c4[0] == '0' && (s_c4[1] == 'x' || s_c4[1] == 'X') && s_c4[2] == '\0')) {
    cs = decode_hex_0x(s_c4, PROOF_GET_MAX_C4_HEX);
    if (!cs.data) {
      proof_get_error(client, 400, "Invalid c4 client_state");
      return true;
    }
  }

  // signers (optional) from the query string
  bytes_t wk = parse_signers_query(qmark ? qmark + 1 : NULL);

  // s_block is validated to be alphanumeric-only, so it cannot break out of the JSON string.
  // eth_getBlockByNumber keeps the includeTx=false suffix to match the payload the client would
  // send in POST mode; header/receipts only take the block identifier.
  char* method_str = strdup(s_method);
  char* params_str = is_block ? bprintf(NULL, "[\"%s\",false]", s_block)
                              : bprintf(NULL, "[\"%s\"]", s_block);

  char cc_buf[96];
  c4_eth_block_cache_control(cc_buf, sizeof cc_buf, s_block, (chain_id_t) http_server.chain_id);

  c4_proof_request_dispatch(client, method_str, params_str, version_num, extra_flags,
                            cs, wk, NULL, NULL, cc_buf);

  if (cs.data) safe_free(cs.data);
  if (wk.data) safe_free(wk.data);
  return true;
}
