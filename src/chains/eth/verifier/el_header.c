/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "el_header.h"
#include "beacon_types.h"
#include "rlp.h"
typedef bytes_t (*el_header_field_func)(void*, buffer_t*, char*);
static const char* el_header_field_names[] = {
    "H:parentHash",            // 0
    "H:sha3Uncles",            // 1
    "A:feeRecipient",          // 2
    "H:stateRoot",             // 3
    "H:transactionsRoot",      // 4
    "H:receiptsRoot",          // 5
    "B:logsBloom",             // 6
    "U:difficulty",            // 7
    "U:blockNumber",           // 8
    "U:gasLimit",              // 9
    "U:gasUsed",               // 10
    "U:timestamp",             // 11
    "B:extraData",             // 12
    "H:prevRandao",            // 13
    "B:nonce",                 // 14
    "U:baseFeePerGas",         // 15
    "H:withdrawalsRoot",       // 16
    "U:blobGasUsed",           // 17
    "U:excessBlobGas",         // 18
    "H:parentBeaconBlockRoot", // 19
    "H:requestsHash",          // 20
    "H:blockAccessListHash",   // 21
    "U:slotNumber",            // 22
};

const char* EMPTY_RLP_LIST = "\x1d\xcc\x4d\xe8\xde\xc7\x5d\x7a\xab\x85\xb5\x67\xb6\xcc\xd4\x1a\xd3\x12\x45\x1b\x94\x8a\x74\x13\xf0\xa1\x42\xfd\x40\xd4\x93\x47";

static c4_status_t eth_el_header_build(c4_state_t* state, bytes_t* el_header, fork_id_t fork, void* data, el_header_field_func get_field_func) {
  buffer_t buffer = {0};
  buffer_t result = {0};
  int      count  = sizeof(el_header_field_names) / sizeof(el_header_field_names[0]);
  if (fork < C4_FORK_GLOAS) count -= 2;

  for (int i = 0; i < count; i++) {
    char    type       = el_header_field_names[i][0];
    char*   field_name = el_header_field_names[i] + 2;
    bytes_t value      = {0};
    if (strcmp(field_name, "sha3Uncles") == 0)
      value = bytes(EMPTY_RLP_LIST, 32);
    else if (strcmp(field_name, "difficulty") == 0)
      value = bytes("\x00", 1);
    else if (strcmp(field_name, "nonce") == 0)
      value = bytes("\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    else
      value = get_field_func(data, &buffer, field_name);
    switch (type) {
      case 'H':
      case 'A': {
        bytes32_t data     = {0};
        int       must_len = type == 'H' ? 32 : 20;
        int       copy_len = must_len < value.len ? must_len : value.len;
        memcpy(data + must_len - copy_len, value.data, copy_len);
        rlp_add_item(&result, bytes(data, must_len));
        break;
      }

      case 'B':
        rlp_add_item(&result, value);
        break;
      case 'U':
        rlp_add_uint(&result, value);
        break;
      default:
        break;
    }
  }
  rlp_to_list(&result);
  *el_header = result.data;
  buffer_free(&buffer);

  return C4_SUCCESS;
}

static bytes_t get_from_ep(void* data, buffer_t* buffer, char* name) {
  ssz_ob_t* ep    = (ssz_ob_t*) data;
  ssz_ob_t  field = ssz_get(ep, name);
  if (field.def == NULL) return NULL_BYTES;
  if (field.def->type == SSZ_TYPE_UINT) {
    buffer_reset(buffer);
    buffer_grow(buffer, field.bytes.len);
    for (int i = 0; i < field.bytes.len; i++) buffer->data.data[i] = field.bytes.data[field.bytes.len - i - 1];
    buffer->data.len = field.bytes.len;
    return buffer->data;
  }
  return field.bytes;
}

c4_status_t eth_el_header_build_from_ep(c4_state_t* state, bytes_t* el_header, fork_id_t fork, ssz_ob_t ep) {
  return eth_el_header_build(state, el_header, fork, &ep, get_from_ep);
}

static bytes_t get_from_json(void* data, buffer_t* buffer, char* name) {
  json_t* json  = (json_t*) data;
  json_t  field = json_get(*json, name);
  if (field.type == JSON_TYPE_NOT_FOUND) return NULL_BYTES;
  return json_as_bytes(field, buffer);
}

c4_status_t eth_el_header_build_from_json(c4_state_t* state, bytes_t* el_header, fork_id_t fork, json_t block) {
  return eth_el_header_build(state, el_header, fork, &block, get_from_json);
}

bytes_t eth_el_header_get(bytes_t header, char* name) {
  int count = sizeof(el_header_field_names) / sizeof(el_header_field_names[0]);
  for (int i = 0; i < count; i++) {
    char* field_name = el_header_field_names[i] + 2;
    if (strcmp(name, field_name) == 0) {
      if (rlp_decode(&header, 0, &header) != RLP_LIST) return NULL_BYTES;
      if (rlp_decode(&header, i, &header) != RLP_ITEM) return NULL_BYTES;
      return header;
    }
  }
  return NULL_BYTES;
}