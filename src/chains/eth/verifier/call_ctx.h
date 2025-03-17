#ifndef CALL_CTX_H
#define CALL_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "eth_account.h"
#include "ssz.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>

typedef struct changed_storage {
  bytes32_t               key;
  bytes32_t               value;
  struct changed_storage* next;

} changed_storage_t;

typedef struct changed_account {
  address_t               address;
  bytes32_t               balance;
  bytes_t                 code;
  struct changed_account* next;
  changed_storage_t*      storage;
  bool                    deleted;
  bool                    free_code;
} changed_account_t;

// Context for EVM execution
typedef struct evmone_context {
  void*              executor;
  verify_ctx_t*      ctx;
  ssz_ob_t           src_accounts;
  changed_account_t* changed_accounts;
  // Current block info
  uint64_t  block_number;
  bytes32_t block_hash;
  uint64_t  timestamp;
  // Transaction info
  bytes32_t tx_origin;
  uint64_t  gas_price;
  // For storing results
  struct evmone_context* parent;
  void*                  results;
} evmone_context_t;

static ssz_ob_t get_src_account(evmone_context_t* ctx, const address_t address) {
  size_t len = ssz_len(ctx->src_accounts);
  for (int i = 0; i < len; i++) {
    ssz_ob_t account = ssz_at(ctx->src_accounts, i);
    bytes_t  addr    = ssz_get(&account, "address").bytes;
    if (memcmp(addr.data, address, 20) == 0)
      return account;
  }
  if (ctx->parent)
    return get_src_account(ctx->parent, address);
  return (ssz_ob_t) {0};
}

static void get_src_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result) {
  ssz_ob_t account = get_src_account(ctx, address);
  if (!account.def) return;
  ssz_ob_t storage = ssz_get(&account, "storageProof");
  uint32_t len     = ssz_len(storage);
  for (int i = 0; i < len; i++) {
    ssz_ob_t entry = ssz_at(storage, i);
    if (memcmp(ssz_get(&entry, "key").bytes.data, key, 32) == 0) {
      if (!eth_get_storage_value(entry, result)) memset(result, 0, 32);
      return;
    }
  }
  if (ctx->parent)
    get_src_storage(ctx->parent, address, key, result);
}

static changed_account_t* get_changed_account(evmone_context_t* ctx, const address_t address) {
  for (changed_account_t* acc = ctx->changed_accounts; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  if (ctx->parent)
    return get_changed_account(ctx->parent, address);
  return NULL;
}

static changed_storage_t* get_changed_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key) {
  changed_account_t* account = get_changed_account(ctx, addr);
  if (!account) return NULL;
  for (changed_storage_t* s = account->storage; s != NULL; s = s->next) {
    if (memcmp(s->key, key, 32) == 0)
      return s;
  }
  return NULL;
}

static changed_account_t* create_changed_account(evmone_context_t* ctx, const address_t address, bool* created) {
  *created = false;
  for (changed_account_t* acc = ctx->changed_accounts; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  changed_account_t* parent_acc  = ctx->parent ? get_changed_account(ctx->parent, address) : NULL;
  *created                       = parent_acc == NULL;
  ssz_ob_t           old_account = get_src_account(ctx, address);
  changed_account_t* acc         = calloc(1, sizeof(changed_account_t));
  memcpy(acc->address, address, 20);
  acc->next             = ctx->changed_accounts;
  ctx->changed_accounts = acc;

  if (parent_acc) {
    memcpy(acc->balance, parent_acc->balance, 32);
    acc->code                       = parent_acc->code;
    changed_storage_t** storage_ptr = &acc->storage;
    for (changed_storage_t* s = parent_acc->storage; s != NULL; s = s->next) {
      *storage_ptr = calloc(1, sizeof(changed_storage_t));
      memcpy((*storage_ptr)->key, s->key, 32);
      memcpy((*storage_ptr)->value, s->value, 32);
      (*storage_ptr)->next = NULL;
      storage_ptr          = &(*storage_ptr)->next;
    }
  }
  else if (old_account.def) {
    ssz_ob_t code = ssz_get(&old_account, "code");
    if (code.def && code.def->type == SSZ_TYPE_LIST && code.bytes.len > 0) acc->code = code.bytes;
    eth_get_account_value(old_account, ETH_ACCOUNT_BALANCE, acc->balance);
  }
  return acc;
}

static void set_changed_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key, const bytes32_t value, bool* account_created, bool* storage_created) {
  changed_storage_t* storage = get_changed_storage(ctx, addr, key);
  if (storage) {
    memcpy(storage->value, value, 32);
    *account_created = false;
    *storage_created = false;
  }
  else {
    changed_account_t* account = create_changed_account(ctx, addr, account_created);
    *storage_created           = true;
    changed_storage_t* storage = calloc(1, sizeof(changed_storage_t));
    memcpy(storage->key, key, 32);
    memcpy(storage->value, value, 32);
    storage->next    = account->storage;
    account->storage = storage;
  }
}
static bytes_t get_code(evmone_context_t* ctx, const address_t address) {
  changed_account_t* changed_account = get_changed_account(ctx, address);
  if (changed_account) return changed_account->code;
  ssz_ob_t account = get_src_account(ctx, address);
  if (!account.def) return NULL_BYTES;
  ssz_ob_t code = ssz_get(&account, "code");
  if (code.def && code.def->type == SSZ_TYPE_LIST) return code.bytes;
  return NULL_BYTES;
  //  return account.def ? ssz_get(&account, "code").bytes : NULL_BYTES;
}

static void changed_account_free(changed_account_t* acc) {
  while (acc->storage) {
    changed_storage_t* storage = acc->storage;
    acc->storage               = storage->next;
    free(storage);
  }
  if (acc->code.data && acc->free_code)
    free(acc->code.data);
  free(acc);
}

static void context_free(evmone_context_t* ctx) {
  while (ctx->changed_accounts) {
    changed_account_t* next = ctx->changed_accounts->next;
    changed_account_free(ctx->changed_accounts);
    ctx->changed_accounts = next;
  }
}

static void context_apply(evmone_context_t* ctx) {
  if (!ctx->parent) return;
  bool created;
  for (changed_account_t* acc = ctx->changed_accounts; acc; acc = acc->next) {
    changed_account_t* parent_acc = create_changed_account(ctx->parent, acc->address, &created);
    memcpy(parent_acc->balance, acc->balance, 32);
    parent_acc->code      = acc->code;
    parent_acc->free_code = acc->free_code;

    for (changed_storage_t* s = acc->storage; s; s = s->next)
      set_changed_storage(ctx->parent, acc->address, s->key, s->value, &created, &created);
  }
}
#ifdef __cplusplus
}
#endif

#endif /* CALL_CTX_H */