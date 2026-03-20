/*
 * Keccak-256 bridge for evmone.
 *
 * Provides ethash_keccak256() using colibri's SHA3/Keccak library,
 * with an optional thread-local hook for intercepting KECCAK256 opcode data.
 *
 * SPDX-License-Identifier: MIT
 */
#include "evmone_precompiles/hash_types.h"
#include "evmone_precompiles/keccak.h"
#include "evmone_c_wrapper.h"
#include <sha3.h>

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

static THREAD_LOCAL evmone_keccak_fn keccak_hook_fn  = NULL;
static THREAD_LOCAL void*            keccak_hook_ctx = NULL;

void evmone_set_keccak_hook(evmone_keccak_fn fn, void* hook_ctx) {
  keccak_hook_fn  = fn;
  keccak_hook_ctx = hook_ctx;
}

union ethash_hash256 ethash_keccak256(const uint8_t* data, size_t size) {
  union ethash_hash256 result = {{{0}}};
  SHA3_CTX             sha_ctx;
  sha3_256_Init(&sha_ctx);
  sha3_Update(&sha_ctx, data, size);
  keccak_Final(&sha_ctx, result.bytes);

  if (keccak_hook_fn)
    keccak_hook_fn(keccak_hook_ctx, data, size, result.bytes);

  return result;
}
