
#include "eth_account.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "plugin.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const uint8_t* EMPTY_HASH      = (uint8_t*) "\xc5\xd2\x46\x01\x86\xf7\x23\x3c\x92\x7e\x7d\xb2\xdc\xc7\x03\xc0\xe5\x00\xb6\x53\xca\x82\x27\x3b\x7b\xfa\xd8\x04\x5d\x85\xa4\x70";
const uint8_t* EMPTY_ROOT_HASH = (uint8_t*) "\x56\xe8\x1f\x17\x1b\xcc\x55\xa6\xff\x83\x45\xe6\x92\xc0\xf8\x6e\x5b\x48\xe0\x1b\x99\x6c\xad\xc0\x01\x62\x2f\xb5\xe3\x63\xb4\x21";
static void    remove_leading_zeros(bytes_t* value) {
  while (value->len > 0 && value->data[0] == 0) {
    value->data++;
    value->len--;
  }
}
static bool is_equal(ssz_ob_t expect, bytes_t* list, int index) {
  bytes_t value;
  if (rlp_decode(list, index, &value) != RLP_ITEM) return false;
  bytes_t exp = expect.bytes;
  remove_leading_zeros(&value);
  remove_leading_zeros(&exp);
  return value.len == exp.len && memcmp(exp.data, value.data, exp.len) == 0;
}

static bool verify_storage(verify_ctx_t* ctx, ssz_ob_t storage_proofs, bytes32_t storage_hash, bytes_t values) {
  if (values.data) memset(values.data, 0, 32);
  int len = ssz_len(storage_proofs);
  if (len != 0 && memcmp(storage_hash, EMPTY_ROOT_HASH, 32) == 0) RETURN_VERIFY_ERROR(ctx, "invalid storage proof because an empty storage hash can not have values!");
  for (int i = 0; i < len; i++) {
    bytes32_t path    = {0};
    bytes32_t root    = {0};
    ssz_ob_t  storage = ssz_at(storage_proofs, i);
    ssz_ob_t  proof   = ssz_get(&storage, "proof");
    ssz_ob_t  key     = ssz_get(&storage, "key");
    bytes_t   leaf    = {0};
    keccak(key.bytes, path);
    if (patricia_verify(root, bytes(path, 32), proof, &leaf) == PATRICIA_INVALID) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");
    if (memcmp(root, storage_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "invalid storage root!");
    if (values.data && values.len >= (i + 1) * 32 && rlp_decode(&leaf, 0, &leaf) == RLP_ITEM)
      memcpy(values.data + (i + 1) * 32 - leaf.len, leaf.data, leaf.len);
  }

  return true;
}

INTERNAL bool eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes_t values) {
  ssz_ob_t  account_proof = ssz_get(proof, "accountProof");
  ssz_ob_t  address       = ssz_get(proof, "address");
  bytes32_t address_hash  = {0};
  bytes_t   rlp_account   = {0};
  bytes_t   field_value   = {0};
  bytes32_t storage_hash  = {0};

  keccak(address.bytes, address_hash);

  switch (field) {
    case ETH_ACCOUNT_CODE_HASH:
      memcpy(values.data, EMPTY_HASH, 32);
      break;
    case ETH_ACCOUNT_STORAGE_HASH:
      memcpy(values.data, EMPTY_ROOT_HASH, 32);
      break;
    default:
      memset(values.data, 0, 32);
      break;
  }

  patricia_result_t result = patricia_verify(state_root, bytes(address_hash, 32), account_proof, &rlp_account);
  if (result == PATRICIA_INVALID) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
  if (result == PATRICIA_FOUND) { // 2 means not existing account
    if (!rlp_account.data) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");

    if (rlp_decode(&rlp_account, 0, &rlp_account) != RLP_LIST) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
    if (rlp_decode(&rlp_account, 2, &field_value) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid account proof for storage hash!");
    // get the storage hash from the proof
    if (field_value.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid account proof for storage hash!");
    memcpy(storage_hash, field_value.data, field_value.len);

    // get the field value from the proof
    if (field) {
      if (rlp_decode(&rlp_account, field - 1, &field_value) != RLP_ITEM) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      if (field_value.len > 32) RETURN_VERIFY_ERROR(ctx, "invalid account proof on execution layer!");
      memcpy(values.data + 32 - field_value.len, field_value.data, field_value.len);
    }
  }

  if (!verify_storage(ctx, ssz_get(proof, "storageProof"), storage_hash, field == ETH_ACCOUNT_STORAGE_HASH ? values : NULL_BYTES)) RETURN_VERIFY_ERROR(ctx, "invalid storage proof!");

  return true;
}

static bytes_t get_last_value(ssz_ob_t proof) {
  bytes_t last_value = ssz_at(proof, ssz_len(proof) - 1).bytes;
  if (!last_value.data) return NULL_BYTES;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_LIST) return NULL_BYTES;
  switch ((int) rlp_decode(&last_value, -1, &last_value)) {
    case 2: // must be a leaf (otherwise the verification would have failed)
      if (rlp_decode(&last_value, 1, &last_value) != RLP_ITEM) return NULL_BYTES;
      break;
    case 17: // branch noch with the value
      if (rlp_decode(&last_value, 16, &last_value) != RLP_ITEM) return NULL_BYTES;
      break;
    default:
      return NULL_BYTES;
  }
  return last_value;
}

bool eth_get_storage_value(ssz_ob_t storage, bytes32_t value) {
  bytes_t last_value = get_last_value(ssz_get(&storage, "proof"));
  if (!last_value.data) return false;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_ITEM) return false;
  if (last_value.len > 32) return false;
  memcpy(value + 32 - last_value.len, last_value.data, last_value.len);
  return true;
}

void eth_get_account_value(ssz_ob_t account, eth_account_field_t field, bytes32_t value) {
  bytes_t last_value = get_last_value(ssz_get(&account, "accountProof"));
  if (!last_value.data) return;
  if (rlp_decode(&last_value, 0, &last_value) != RLP_LIST) return;
  if (rlp_decode(&last_value, field - 1, &last_value) != RLP_ITEM) return;
  if (last_value.len > 32) return;
  memcpy(value + 32 - last_value.len, last_value.data, last_value.len);
}

INTERNAL c4_status_t eth_get_call_codes(verify_ctx_t* ctx, call_code_t** call_codes, ssz_ob_t accounts) {
  c4_status_t      status = C4_SUCCESS;
  storage_plugin_t cache  = {0};
  bytes32_t        hash   = {0};
  char             tmp[200];
  buffer_t         buf = stack_buffer(tmp);
  c4_get_storage_config(&cache);

  uint32_t len = ssz_len(accounts);
  for (uint32_t i = 0; i < len; i++) {
    ssz_ob_t acc  = ssz_at(accounts, i);
    ssz_ob_t code = ssz_get(&acc, "code");

    if (code.def->type == SSZ_TYPE_BOOLEAN && code.bytes.data[0] == 0) continue; // no code which might be relevant for us

    call_code_t* ac = (call_code_t*) safe_calloc(1, sizeof(call_code_t));
    eth_get_account_value(acc, ETH_ACCOUNT_CODE_HASH, ac->hash);

    // fetch from cache
    buffer_reset(&buf);
    buffer_t data = {0};
    if (memcmp(ac->hash, EMPTY_HASH, 32) == 0) { // empty code
      ac->code = NULL_BYTES;
      ac->free = false;
    }
    else if (cache.get(bprintf(&buf, "code_%x", bytes(ac->hash, 32)), &data)) {
      ac->code = data.data;
      ac->free = true;
    }
    else if (code.def->type == SSZ_TYPE_LIST) { // code is part of the proof, but not cached yet
      ac->code = code.bytes;
      ac->free = false;

      // store in cache
      cache.set((char*) buf.data.data, ac->code);
    }
    else {
      buffer_reset(&buf);
      bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\": \"eth_getCode\", \"params\": [\"0x%x\", \"latest\"]}", ssz_get(&acc, "address").bytes);
      keccak(buf.data, hash);
      data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, hash);
      if (req && req->response.data) {
        buffer_reset(&buf);
        json_t result = json_get(json_parse((char*) req->response.data), "result");
        if (result.type == JSON_TYPE_STRING) {
          buffer_t code_data = {0};
          ac->code           = json_as_bytes(result, &code_data);
          ac->free           = true;

          keccak(ac->code, hash);
          if (memcmp(hash, ac->hash, 32) != 0) { // code hash mismatch
            eth_free_codes(ac);
            ac     = NULL;
            status = c4_state_add_error(&ctx->state, "code hash mismatch");
          }
          else // store in cache
            cache.set(bprintf(&buf, "code_%x", bytes(ac->hash, 32)), ac->code);
        }
        else
          status = c4_state_add_error(&ctx->state, bprintf(&buf, "error fetching code from rpc: %s", req->response.data));
      }
      else {
        // we need to fecth the code from rpc
        data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
        req->chain_id       = ctx->chain_id;
        req->encoding       = C4_DATA_ENCODING_JSON;
        req->type           = C4_DATA_TYPE_ETH_RPC;
        req->payload        = bytes_dup(buf.data);
        req->method         = C4_DATA_METHOD_POST;
        memcpy(req->id, hash, 32);
        c4_state_add_request(&ctx->state, req);
        if (status != C4_ERROR) status = C4_PENDING;
        safe_free(ac);
        ac = NULL;
      }
    }
    if (ac) {
      call_code_t* next = *call_codes;
      *call_codes       = ac;
      ac->next          = next;
    }
  }

  if (status != C4_SUCCESS) {
    eth_free_codes(*call_codes);
    *call_codes = NULL;
  }

  return status;
}

INTERNAL void eth_free_codes(call_code_t* call_codes) {
  while (call_codes) {
    call_code_t* next = call_codes->next;
    if (call_codes->free) safe_free(call_codes->code.data);
    safe_free(call_codes);
    call_codes = next;
  }
}
