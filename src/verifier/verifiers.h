#include "verify.h"

ssz_def_t* c4_eth_get_request_type(chain_id_t chain_id);
bool       c4_eth_verify(verify_ctx_t* ctx);

static ssz_def_t* request_container(chain_id_t chain_id) {
  ssz_def_t* container = NULL;
  if (!container) container = c4_eth_get_request_type(chain_id);

  return container;
}

static bool handle_verification(verify_ctx_t* ctx) {
  if (c4_eth_verify(ctx)) return true;

  return false;
}