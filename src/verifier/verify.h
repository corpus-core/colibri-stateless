#ifndef verify_h__
#define verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/chains.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  PROOF_TYPE_BEACON_HEADER,
  PROOF_TYPE_SYNC_COMMITTEE,
  PROOF_TYPE_ACCOUNT,
  PROOF_TYPE_TRANSACTION,
} proof_type_t;

typedef struct {
  proof_type_t type;
  char*        method;
  json_t       args;
  ssz_ob_t     proof;
  ssz_ob_t     data;
  ssz_ob_t     sync_data;
  uint64_t     first_missing_period;
  uint64_t     last_missing_period;
  bool         success;
  c4_state_t   state;
  chain_id_t   chain_id;
} verify_ctx_t;

void c4_verify(verify_ctx_t* ctx);
void c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);
bool verify_blockhash_proof(verify_ctx_t* ctx);
bool verify_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);

// helper
bool c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot);
#pragma endregion
#ifdef MESSAGES
#define RETURN_VERIFY_ERROR(ctx, msg)                                             \
  do {                                                                            \
    ctx->state.error = ctx->state.error == NULL ? msg : strdup(ctx->state.error); \
    ctx->success     = false;                                                     \
    return false;                                                                 \
  } while (0)
#else
#define RETURN_VERIFY_ERROR(ctx, msg) \
  do {                                \
    ctx->state.error = "E";           \
    return false;                     \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif