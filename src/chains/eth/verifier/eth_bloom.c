/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "eth_bloom.h"
#include "crypto.h"
#include <string.h>

#define BLOOM_BYTE_LENGTH 256u

static inline void bloom_set(uint8_t* bloom, uint16_t idx) {
  uint16_t byte_index = (uint16_t) (BLOOM_BYTE_LENGTH - 1u - ((idx >> 3u) & 0xffu));
  uint8_t  bit_mask   = (uint8_t) (1u << (idx & 7u));
  bloom[byte_index] |= bit_mask;
}

static void bloom_add_element_buf(uint8_t bloom[256], bytes_t element) {
  bytes32_t hash = {0};
  keccak(element, hash);

  for (int i = 0; i < 6; i += 2) {
    uint16_t idx = (uint16_t) ((((uint16_t) hash[i] << 8u) | hash[i + 1]) & 0x7ffu);
    bloom_set(bloom, idx);
  }
}

void c4_eth_parse_filter_addresses(json_t address_json, bytes_t* out_addresses) {
  uint8_t  tmp[ADDRESS_SIZE] = {0};
  buffer_t b                 = stack_buffer(tmp);
  switch (address_json.type) {
    case JSON_TYPE_STRING: {
      bytes_t a = json_as_bytes(address_json, &b);
      if (a.len == ADDRESS_SIZE) {
        *out_addresses = bytes_dup(a);
      }
      return;
    }
    case JSON_TYPE_ARRAY: {
      int count = 0;
      json_for_each_value(address_json, _) count++;
      if (count <= 0) return;
      uint8_t* buf = (uint8_t*) safe_malloc((size_t) count * ADDRESS_SIZE);
      int      i   = 0;
      json_for_each_value(address_json, a) {
        bytes_t ab = json_as_bytes(a, &b);
        if (ab.len == ADDRESS_SIZE) memcpy(buf + (i++ * ADDRESS_SIZE), ab.data, ADDRESS_SIZE);
      }
      if (i == 0) {
        safe_free(buf);
        return;
      }
      *out_addresses = bytes(buf, (uint32_t) (i * ADDRESS_SIZE));
      return;
    }
    default:
      return;
  }
}

void c4_eth_parse_filter_topics(json_t topics_json, bytes_t out_topics[C4_ETH_LOG_MAX_TOPICS]) {
  memset(out_topics, 0, sizeof(bytes_t) * C4_ETH_LOG_MAX_TOPICS);
  if (topics_json.type != JSON_TYPE_ARRAY) return;
  uint8_t  tmp[32] = {0};
  buffer_t b       = stack_buffer(tmp);
  int      pos     = 0;
  json_for_each_value(topics_json, tpos) {
    if (pos >= C4_ETH_LOG_MAX_TOPICS) break;
    if (tpos.type == JSON_TYPE_STRING) {
      bytes_t v = json_as_bytes(tpos, &b);
      if (v.len == 32) {
        uint8_t* buf = (uint8_t*) safe_malloc(32);
        memcpy(buf, v.data, 32);
        out_topics[pos] = bytes(buf, 32);
      }
    }
    else if (tpos.type == JSON_TYPE_ARRAY) {
      int count = json_len(tpos);
      if (count > 0) {
        uint8_t* buf = (uint8_t*) safe_malloc((size_t) count * 32);
        int      i   = 0;
        json_for_each_value(tpos, cand) {
          bytes_t v = json_as_bytes(cand, &b);
          if (v.len == 32) memcpy(buf + (i++ * 32), v.data, 32);
        }
        out_topics[pos] = bytes(buf, (uint32_t) (32 * i));
      }
    }
    pos++;
  }
}

static int build_bloom_variants(bytes_t addresses, bytes_t topics[C4_ETH_LOG_MAX_TOPICS], uint64_t out_variants[C4_ETH_BLOOM_MAX_VARIANTS][32]) {
  int addr_count                    = (int) (addresses.len / ADDRESS_SIZE);
  int counts[C4_ETH_LOG_MAX_TOPICS] = {0};
  for (int p = 0; p < C4_ETH_LOG_MAX_TOPICS; p++) counts[p] = (int) topics[p].len / 32;
  int total = (addr_count ? addr_count : 1);
  for (int p = 0; p < C4_ETH_LOG_MAX_TOPICS; p++) {
    int c = counts[p] ? counts[p] : 1;
    if (total > (C4_ETH_BLOOM_MAX_VARIANTS / c)) return 0;
    total *= c;
  }
  int idx_addr                   = 0;
  int idx[C4_ETH_LOG_MAX_TOPICS] = {0, 0, 0, 0};
  for (int v = 0; v < total && v < C4_ETH_BLOOM_MAX_VARIANTS; v++) {
    uint8_t* bloom = (uint8_t*) out_variants[v];
    memset(bloom, 0, 256);
    if (addr_count) bloom_add_element_buf(bloom, bytes(addresses.data + (idx_addr * ADDRESS_SIZE), ADDRESS_SIZE));
    for (int p = 0; p < C4_ETH_LOG_MAX_TOPICS; p++) {
      if (!counts[p]) continue;
      bloom_add_element_buf(bloom, bytes(topics[p].data + (idx[p] * 32), 32));
    }
    if (addr_count) {
      idx_addr++;
      if (idx_addr < addr_count) continue;
      idx_addr = 0;
    }
    for (int p = C4_ETH_LOG_MAX_TOPICS - 1; p >= 0; p--) {
      if (counts[p] < 2) continue;
      idx[p]++;
      if (idx[p] < counts[p]) break;
      idx[p] = 0;
    }
  }
  return total;
}

bytes_t c4_eth_filter_query_blooms(json_t filter) {
  json_t bloom_filter = json_get(filter, "bloomFilter");
  if (bloom_filter.type == JSON_TYPE_ARRAY) {
    buffer_t out = {0};
    uint8_t  tmp[256];
    buffer_t b = stack_buffer(tmp);
    json_for_each_value(bloom_filter, entry) {
      bytes_t v = json_as_bytes(entry, &b);
      if (v.len != BLOOM_BYTE_LENGTH) {
        buffer_free(&out);
        return NULL_BYTES; // malformed PAP bloom -> cannot decide negativity
      }
      buffer_append(&out, v);
    }
    return out.data;
  }
  return c4_eth_create_bloomfilter(filter);
}

bool c4_eth_bloom_negative(bytes_t query_blooms, bytes_t block_bloom) {
  if (block_bloom.len != BLOOM_BYTE_LENGTH) return false;
  uint32_t variant_count = query_blooms.len / BLOOM_BYTE_LENGTH;
  if (variant_count == 0) return false; // no variants -> cannot prove absence

  for (uint32_t v = 0; v < variant_count; v++) {
    const uint8_t* q         = query_blooms.data + (size_t) v * BLOOM_BYTE_LENGTH;
    const uint8_t* b         = block_bloom.data;
    bool           is_subset = true;
    // Compare 8 bytes per step (BLOOM_BYTE_LENGTH is a multiple of 8): the variant
    // is a subset iff no required bit is missing in the block bloom, i.e. (q & ~b) == 0
    // for every word. Endianness is irrelevant since we only test whether any word has
    // a bit set in q but not in b. memcpy avoids unaligned reads and is lowered to a
    // single load by the compiler.
    for (uint32_t i = 0; i < BLOOM_BYTE_LENGTH; i += 8) {
      uint64_t qi, bi;
      memcpy(&qi, q + i, sizeof(qi));
      memcpy(&bi, b + i, sizeof(bi));
      if (qi & ~bi) {
        is_subset = false; // a required bit is missing in the block bloom
        break;
      }
    }
    if (is_subset) return false; // this variant might match -> not bloom-negative
  }
  return true; // no variant can match -> provably no matching log
}

bytes_t c4_eth_create_bloomfilter(json_t filter) {
  bytes_t result                        = {0};
  bytes_t addresses                     = {0};
  bytes_t topics[C4_ETH_LOG_MAX_TOPICS] = {0};
  c4_eth_parse_filter_addresses(json_get(filter, "address"), &addresses);
  c4_eth_parse_filter_topics(json_get(filter, "topics"), topics);
  uint64_t tmp_variants[C4_ETH_BLOOM_MAX_VARIANTS][32];
  int      vcount = build_bloom_variants(addresses, topics, tmp_variants);
  if (vcount > 0) {
    result = bytes(safe_malloc((size_t) vcount * 256), (uint32_t) (vcount * 256));
    memcpy(result.data, tmp_variants, (size_t) vcount * 256);
  }
  safe_free(addresses.data);
  for (int i = 0; i < C4_ETH_LOG_MAX_TOPICS; i++) safe_free(topics[i].data);
  return result;
}

static bool log_address_matches(bytes_t filter_addresses, bytes_t log_address) {
  if (filter_addresses.len == 0) return true;
  for (uint32_t i = 0; i < filter_addresses.len; i += ADDRESS_SIZE) {
    if (log_address.len == ADDRESS_SIZE && memcmp(log_address.data, filter_addresses.data + i, ADDRESS_SIZE) == 0)
      return true;
  }
  return false;
}

static bool log_topics_match(bytes_t filter_topics[C4_ETH_LOG_MAX_TOPICS], ssz_ob_t log_topics_ssz) {
  uint32_t log_topics_count = ssz_len(log_topics_ssz);
  for (int p = 0; p < C4_ETH_LOG_MAX_TOPICS; p++) {
    if (filter_topics[p].len == 0) continue;
    if (log_topics_count <= (uint32_t) p) return false;
    bytes_t log_topic = ssz_at(log_topics_ssz, (uint32_t) p).bytes;
    bool    any       = false;
    for (uint32_t i = 0; i < filter_topics[p].len; i += 32) {
      if (log_topic.len >= 32 && memcmp(log_topic.data, filter_topics[p].data + i, 32) == 0) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  return true;
}

ssz_ob_t c4_eth_filter_logs(ssz_ob_t logs, json_t filter) {
  bytes_t addresses                     = {0};
  bytes_t topics[C4_ETH_LOG_MAX_TOPICS] = {0};

  c4_eth_parse_filter_addresses(json_get(filter, "address"), &addresses);
  c4_eth_parse_filter_topics(json_get(filter, "topics"), topics);

  ssz_builder_t builder = ssz_builder_for_def(logs.def);
  uint32_t      count   = ssz_len(logs);
  uint32_t      matched = 0;

  for (uint32_t i = 0; i < count; i++) {
    ssz_ob_t log      = ssz_at(logs, i);
    bytes_t  log_addr = ssz_get(&log, "address").bytes;
    if (!log_address_matches(addresses, log_addr)) continue;
    ssz_ob_t log_topics = ssz_get(&log, "topics");
    if (!log_topics_match(topics, log_topics)) continue;
    ssz_add_dynamic_list_bytes(&builder, 0, log.bytes);
    matched++;
  }

  ssz_builder_fix_list_offsets(&builder, matched);

  safe_free(addresses.data);
  for (int i = 0; i < C4_ETH_LOG_MAX_TOPICS; i++) safe_free(topics[i].data);

  return ssz_builder_to_bytes(&builder);
}
