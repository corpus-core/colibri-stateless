#include "../util/json.h"
#include "../util/logger.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "proofs.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

c4_status_t get_eth_tx(proofer_ctx_t* ctx, json_t txhash, json_t* tx_data) {
  uint8_t  tmp[200];
  buffer_t buf = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, "eth_getTransactionByHash", bprintf(&buf, "[%J]", txhash), tx_data);
}

c4_status_t eth_getBlockReceipts(proofer_ctx_t* ctx, json_t block, json_t* receipts_array) {
  uint8_t  tmp[200];
  buffer_t buf = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, "eth_getBlockReceipts", bprintf(&buf, "[%J]", block), receipts_array);
}
