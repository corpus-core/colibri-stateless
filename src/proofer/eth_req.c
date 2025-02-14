#include "../util/json.h"
#include "../util/logger.h"
#include "../util/rlp.h"
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

bytes_t c4_serialize_receipt(json_t r, buffer_t* buf) {
  uint8_t  tmp[300]   = {0};
  buffer_t tmp_buf    = stack_buffer(tmp);
  uint8_t  tmp2[32]   = {0};
  buffer_t short_buf  = stack_buffer(tmp2);
  buffer_t topics_buf = {0};
  buffer_t logs_buf   = {0};
  buffer_t log_buf    = {0};
  buf->data.len       = 0;
  uint8_t type        = json_get_uint8(r, "type");
  uint8_t status      = json_get_uint8(r, "status");
  bytes_t state_root  = json_get_bytes(r, "stateRoot", &tmp_buf);

  if (state_root.len == 32)
    rlp_add_item(buf, state_root);
  else
    rlp_add_uint64(buf, (uint64_t) status);
  rlp_add_uint64(buf, json_get_uint64(r, "cumulativeGasUsed"));
  rlp_add_item(buf, json_get_bytes(r, "logsBloom", &tmp_buf));

  json_for_each_value(json_get(r, "logs"), log) {
    log_buf.data.len = 0;
    rlp_add_item(&log_buf, json_get_bytes(log, "address", &tmp_buf));

    topics_buf.data.len = 0;
    json_for_each_value(json_get(log, "topics"), topic)
        rlp_add_item(&topics_buf, json_as_bytes(topic, &short_buf));
    rlp_add_list(&log_buf, topics_buf.data);
    rlp_add_item(&log_buf, json_get_bytes(log, "data", &topics_buf));
    rlp_add_list(&logs_buf, log_buf.data);
  }
  rlp_add_list(buf, logs_buf.data);
  rlp_to_list(buf);
  if (type) buffer_splice(buf, 0, 0, bytes(&type, 1));
  buffer_free(&logs_buf);
  buffer_free(&log_buf);
  buffer_free(&topics_buf);
  //  print_hex(stderr, buf->data, "serialize_receipt: 0x", "\n");
  return buf->data;
}
