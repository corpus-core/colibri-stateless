#include "beacon.h"
#include "eth_req.h"
#include "json.h"
#include "proofer.h"
#include <inttypes.h>
#include <string.h>
#ifdef PROOFER_CACHE
#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <sys/time.h>
#endif

typedef struct {
  uint64_t slot;
  uint64_t block_number;
  uint8_t  blockhash[32];
} block_number_t;

typedef struct {
  chain_id_t chain_id;
  buffer_t   block_numbers;
  uint64_t   beacon_latest;
  uint64_t   beacon_timestamp;
} chain_blocks_t;

static uint32_t max_entries = 1000;
static buffer_t chains      = {0};
#define CHAIN_SIZE sizeof(chain_blocks_t)
#define BLOCK_SIZE sizeof(block_number_t)

uint64_t current_ms() {
  struct timeval te;
#ifdef _WIN32
  FILETIME       ft;
  ULARGE_INTEGER li;
  GetSystemTimeAsFileTime(&ft);
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  // Convert to microseconds from Jan 1, 1601
  // Then adjust to Unix epoch (Jan 1, 1970)
  uint64_t unix_time = (li.QuadPart - 116444736000000000LL) / 10;
  te.tv_sec          = unix_time / 1000000;
  te.tv_usec         = unix_time % 1000000;
#else
  gettimeofday(&te, NULL);
#endif
  return te.tv_sec * 1000L + te.tv_usec / 1000;
}

uint64_t c4_beacon_cache_get_slot(json_t block, chain_id_t chain_id) {
  bytes32_t       blockhash    = {0};
  uint64_t        block_number = 0;
  chain_blocks_t* chain_blocks = NULL;
  for (uint32_t i = 0; i < chains.data.len / CHAIN_SIZE; i++) {
    chain_blocks_t* blocks = (chain_blocks_t*) (chains.data.data + i * CHAIN_SIZE);
    if (blocks->chain_id == chain_id) {
      chain_blocks = blocks;
      break;
    }
  }

  if (!chain_blocks || block.type != JSON_TYPE_STRING) return false;

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    return current_ms() - chain_blocks->beacon_timestamp > 1000 * 6 ? 0 : chain_blocks->beacon_latest;
  else if (strncmp(block.start, "\"0x", 3) == 0) {
    if (block.len == 68)
      hex_to_bytes(block.start + 3, 64, bytes(blockhash, 32));
    else
      block_number = json_as_uint64(block);
  }
  else
    return 0;

  for (uint32_t i = 0; i < chain_blocks->block_numbers.data.len / BLOCK_SIZE; i++) {
    block_number_t* b = (block_number_t*) (chain_blocks->block_numbers.data.data + i * BLOCK_SIZE);
    if (block_number) {
      if (b->block_number == block_number) return b->slot;
    }
    else if (memcmp(b->blockhash, blockhash, 32) == 0)
      return b->slot;
  }

  return 0;
}

void c4_beacon_cache_update(chain_id_t chain_id, uint64_t slot, uint64_t block_number, bytes32_t blockhash, bool is_latest) {
  chain_blocks_t* chain_blocks = NULL;
  for (uint32_t i = 0; i < chains.data.len / CHAIN_SIZE; i++) {
    chain_blocks_t* blocks = (chain_blocks_t*) (chains.data.data + i * CHAIN_SIZE);
    if (blocks->chain_id == chain_id) {
      chain_blocks = blocks;
      break;
    }
  }

  if (!chain_blocks) {
    buffer_grow(&chains, chains.data.len + CHAIN_SIZE);
    chain_blocks = (chain_blocks_t*) (chains.data.data + chains.data.len);
    chains.data.len += CHAIN_SIZE;
    chain_blocks->chain_id         = chain_id;
    chain_blocks->block_numbers    = (buffer_t) {0};
    chain_blocks->beacon_latest    = 0;
    chain_blocks->beacon_timestamp = 0;
  }

  uint32_t        len   = chain_blocks->block_numbers.data.len / BLOCK_SIZE;
  block_number_t* block = NULL;
  if (len == max_entries) {
    for (uint32_t i = 0; i < len; i++) {
      block_number_t* b = (block_number_t*) (chain_blocks->block_numbers.data.data + i * BLOCK_SIZE);
      if (!block || b->slot > block->slot) block = b;
    }
  }
  else {
    buffer_grow(&chain_blocks->block_numbers, chain_blocks->block_numbers.data.len + BLOCK_SIZE);
    block = (block_number_t*) (chain_blocks->block_numbers.data.data + len * BLOCK_SIZE);
  }

  if (block) {
    block->slot         = slot;
    block->block_number = block_number;
    memcpy(block->blockhash, blockhash, 32);
  }

  if (is_latest) {
    chain_blocks->beacon_latest    = slot;
    chain_blocks->beacon_timestamp = current_ms();
  }
}

#endif
