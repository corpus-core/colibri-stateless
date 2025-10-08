/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef CACHE_KEYS_H
#define CACHE_KEYS_H

#include "util/bytes.h"
#include <stdint.h>

// Cache key prefixes for different data types (first byte)
#define CACHE_PREFIX_BEACON_BLOCK   'B' // Beacon block data
#define CACHE_PREFIX_BEACON_SLOT    'S' // Slot-based data
#define CACHE_PREFIX_ETH_RECEIPT    'R' // Receipt merkle trees
#define CACHE_PREFIX_ETH_LOGS       'L' // Log merkle trees
#define CACHE_PREFIX_WITNESS        'W' // Witness data
#define CACHE_PREFIX_FINALITY       'F' // Finality data
#define CACHE_PREFIX_SYNC_COMMITTEE 'C' // Sync committee data
#define CACHE_PREFIX_EXECUTION      'E' // Execution payload data

// Cache key structure for better organization and lookup
typedef struct {
  uint8_t  prefix;       // Data type prefix
  uint8_t  version;      // Schema version for compatibility
  uint16_t chain_id;     // Chain identifier (network specific)
  uint32_t block_number; // Block number (for block-related data)
  uint8_t  hash[24];     // Remaining 24 bytes for hash/identifier
} __attribute__((packed)) structured_cache_key_t;

/**
 * Create a structured cache key for beacon block data
 * @param key Output 32-byte cache key
 * @param chain_id Chain identifier
 * @param slot Beacon slot number
 * @param block_root Block root hash (32 bytes)
 */
static inline void create_beacon_block_cache_key(bytes32_t key, uint16_t chain_id, uint64_t slot, const uint8_t* block_root) {
  structured_cache_key_t* sk = (structured_cache_key_t*) key;
  sk->prefix                 = CACHE_PREFIX_BEACON_BLOCK;
  sk->version                = 1;
  sk->chain_id               = chain_id;
  sk->block_number           = (uint32_t) (slot >> 32); // High 32 bits of slot
  // Use first 24 bytes of block_root + low 32 bits of slot
  memcpy(sk->hash, block_root, 20);
  *((uint32_t*) (sk->hash + 20)) = (uint32_t) slot; // Low 32 bits of slot
}

/**
 * Create a structured cache key for receipt merkle trees
 * @param key Output 32-byte cache key
 * @param chain_id Chain identifier
 * @param block_number Block number
 * @param receipts_root Receipts root hash (32 bytes)
 */
static inline void create_receipt_cache_key(bytes32_t key, uint16_t chain_id, uint64_t block_number, const uint8_t* receipts_root) {
  structured_cache_key_t* sk = (structured_cache_key_t*) key;
  sk->prefix                 = CACHE_PREFIX_ETH_RECEIPT;
  sk->version                = 1;
  sk->chain_id               = chain_id;
  sk->block_number           = (uint32_t) block_number;
  memcpy(sk->hash, receipts_root + 8, 24); // Skip first 8 bytes of hash for uniqueness
}

/**
 * Create a structured cache key for log merkle trees
 * @param key Output 32-byte cache key
 * @param chain_id Chain identifier
 * @param block_number Block number
 * @param logs_bloom Logs bloom filter (first 24 bytes used)
 */
static inline void create_logs_cache_key(bytes32_t key, uint16_t chain_id, uint64_t block_number, const uint8_t* logs_bloom) {
  structured_cache_key_t* sk = (structured_cache_key_t*) key;
  sk->prefix                 = CACHE_PREFIX_ETH_LOGS;
  sk->version                = 1;
  sk->chain_id               = chain_id;
  sk->block_number           = (uint32_t) block_number;
  memcpy(sk->hash, logs_bloom, 24);
}

/**
 * Create a structured cache key for witness data
 * @param key Output 32-byte cache key
 * @param chain_id Chain identifier
 * @param witness_type Type of witness data
 * @param params_hash Hash of witness parameters (28 bytes)
 */
static inline void create_witness_cache_key(bytes32_t key, uint16_t chain_id, uint32_t witness_type, const uint8_t* params_hash) {
  structured_cache_key_t* sk = (structured_cache_key_t*) key;
  sk->prefix                 = CACHE_PREFIX_WITNESS;
  sk->version                = 1;
  sk->chain_id               = chain_id;
  sk->block_number           = witness_type;
  memcpy(sk->hash, params_hash, 24);
}

/**
 * Extract cache key prefix for type-based operations
 * @param key 32-byte cache key
 * @return Cache prefix byte
 */
static inline uint8_t get_cache_key_prefix(const bytes32_t key) {
  return ((const structured_cache_key_t*) key)->prefix;
}

/**
 * Extract chain ID from cache key
 * @param key 32-byte cache key
 * @return Chain identifier
 */
static inline uint16_t get_cache_key_chain_id(const bytes32_t key) {
  return ((const structured_cache_key_t*) key)->chain_id;
}

/**
 * Extract block number from cache key
 * @param key 32-byte cache key
 * @return Block number or slot (depending on prefix)
 */
static inline uint32_t get_cache_key_block_number(const bytes32_t key) {
  return ((const structured_cache_key_t*) key)->block_number;
}

/**
 * Check if two cache keys are related (same prefix and chain)
 * @param key1 First cache key
 * @param key2 Second cache key
 * @return true if keys are related
 */
static inline bool cache_keys_are_related(const bytes32_t key1, const bytes32_t key2) {
  const structured_cache_key_t* sk1 = (const structured_cache_key_t*) key1;
  const structured_cache_key_t* sk2 = (const structured_cache_key_t*) key2;
  return sk1->prefix == sk2->prefix && sk1->chain_id == sk2->chain_id;
}

#endif // CACHE_KEYS_H
