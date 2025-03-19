#ifndef C4_PROOFER_H
#define C4_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/chains.h"
#include "../util/state.h"

typedef enum {
  C4_PROOFER_FLAG_INCLUDE_CODE = 1 << 0,
  C4_PROOFER_FLAG_INCLUDE_DATA = 1 << 1,
} proofer_flag_types_t;

typedef uint32_t proofer_flags_t;

typedef struct {
  char*           method;
  json_t          params;
  bytes_t         proof;
  chain_id_t      chain_id;
  c4_state_t      state;
  proofer_flags_t flags;
} proofer_ctx_t;

// generic proofer context
// to use run the c4_proofer_execute in a loop:

// ```c
// data_request_t* data_request = NULL;
// bytes_t proof = {0};
// char* error = NULL;
// while (true) {
//   switch (c4_proofer_status(ctx)) {
//     case C4_SUCCESS:
//       proof = bytes_dup(ctx->proof);
//       break;
//     case C4_PENDING:
//       while ((data_request = c4_state_get_pending_request(&ctx->state))
//          fetch_data(data_request);
//       break;
//     case C4_ERROR:
//       error = strdup(ctx->state.error);
//       break;
//   }
// }
// c4_proofer_free(ctx);
// ....
// ```

proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id, proofer_flags_t flags); // create a new proofer context
void           c4_proofer_free(proofer_ctx_t* ctx);                                                       // cleanup for the ctx
c4_status_t    c4_proofer_execute(proofer_ctx_t* ctx);                                                    // tries to create the proof, but if there are pending requests, they need to fetched before calling it again.
c4_status_t    c4_proofer_status(proofer_ctx_t* ctx);                                                     // returns the status of the proofer

// proofer functions

c4_status_t c4_proof_account(proofer_ctx_t* ctx);     // creates an account proof
c4_status_t c4_proof_transaction(proofer_ctx_t* ctx); // creates a transaction proof
c4_status_t c4_proof_receipt(proofer_ctx_t* ctx);     // creates a receipt proof
c4_status_t c4_proof_logs(proofer_ctx_t* ctx);        // creates a logs proof

#ifdef __cplusplus
}
#endif

#endif