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

// : Ethereum

// :: Colibri RPC-Methods

// ::: colibri_simulateTransaction
//
// Known event decoding for simulation results. Matches emitted log topics against
// a static table of well-known event signatures (ERC20, ERC721, Uniswap V2, WETH)
// and decodes indexed/non-indexed parameters into SSZ InputParam containers.

#include "bytes.h"
#include "call_ctx.h"
#include "ssz.h"
#include "verify_data_types.h"
#include <stdbool.h>
#include <string.h>

// -- Event parameter descriptor --

typedef struct {
  const char* name;    // parameter name (e.g. "from", "to", "value")
  const char* type;    // ABI type (e.g. "address", "uint256", "uint112")
  bool        indexed; // true = value comes from topic, false = from data
} event_param_def_t;

// -- Known event descriptor --

typedef struct {
  const char*              hash;            // 32-byte keccak256 of the event signature (as string literal)
  const char*              name;            // event name (e.g. "Transfer")
  uint8_t                  param_count;     // number of parameters
  uint8_t                  expected_topics; // expected topic count including signature hash
  const event_param_def_t* params;          // pointer to static parameter definitions
} known_event_t;

// -- ERC20 / ERC721 --

// Transfer(address indexed from, address indexed to, uint256 value)
static const event_param_def_t ERC20_TRANSFER_PARAMS[] = {
    {"from", "address", true},
    {"to", "address", true},
    {"value", "uint256", false},
};

// Transfer(address indexed from, address indexed to, uint256 indexed tokenId)
static const event_param_def_t ERC721_TRANSFER_PARAMS[] = {
    {"from", "address", true},
    {"to", "address", true},
    {"tokenId", "uint256", true},
};

// Approval(address indexed owner, address indexed spender, uint256 value)
static const event_param_def_t ERC20_APPROVAL_PARAMS[] = {
    {"owner", "address", true},
    {"spender", "address", true},
    {"value", "uint256", false},
};

// -- Uniswap V2 --

// Mint(address indexed sender, uint amount0, uint amount1)
static const event_param_def_t UNISWAP_MINT_PARAMS[] = {
    {"sender", "address", true},
    {"amount0", "uint256", false},
    {"amount1", "uint256", false},
};

// Burn(address indexed sender, uint amount0, uint amount1, address indexed to)
static const event_param_def_t UNISWAP_BURN_PARAMS[] = {
    {"sender", "address", true},
    {"amount0", "uint256", false},
    {"amount1", "uint256", false},
    {"to", "address", true},
};

// Swap(address indexed sender, uint amount0In, uint amount1In, uint amount0Out, uint amount1Out, address indexed to)
static const event_param_def_t UNISWAP_SWAP_PARAMS[] = {
    {"sender", "address", true},
    {"amount0In", "uint256", false},
    {"amount1In", "uint256", false},
    {"amount0Out", "uint256", false},
    {"amount1Out", "uint256", false},
    {"to", "address", true},
};

// Sync(uint112 reserve0, uint112 reserve1)
static const event_param_def_t UNISWAP_SYNC_PARAMS[] = {
    {"reserve0", "uint112", false},
    {"reserve1", "uint112", false},
};

// -- WETH --

// Deposit(address indexed dst, uint wad)
static const event_param_def_t WETH_DEPOSIT_PARAMS[] = {
    {"dst", "address", true},
    {"wad", "uint256", false},
};

// Withdrawal(address indexed src, uint wad)
static const event_param_def_t WETH_WITHDRAWAL_PARAMS[] = {
    {"src", "address", true},
    {"wad", "uint256", false},
};

// -- Known events table --

static const known_event_t KNOWN_EVENTS[] = {
    // ERC20 Transfer (3 topics: sig + from + to, value in data)
    {"\xdd\xf2\x52\xad\x1b\xe2\xc8\x9b\x69\xc2\xb0\x68\xfc\x37\x8d\xaa\x95\x2b\xa7\xf1\x63\xc4\xa1\x16\x28\xf5\x5a\x4d\xf5\x23\xb3\xef",
     "Transfer", 3, 3, ERC20_TRANSFER_PARAMS},

    // ERC721 Transfer (4 topics: sig + from + to + tokenId, no data params)
    {"\xdd\xf2\x52\xad\x1b\xe2\xc8\x9b\x69\xc2\xb0\x68\xfc\x37\x8d\xaa\x95\x2b\xa7\xf1\x63\xc4\xa1\x16\x28\xf5\x5a\x4d\xf5\x23\xb3\xef",
     "Transfer", 3, 4, ERC721_TRANSFER_PARAMS},

    // ERC20 Approval (3 topics: sig + owner + spender, value in data)
    {"\x8c\x5b\xe1\xe5\xeb\xec\x7d\x5b\xd1\x4f\x71\x42\x7d\x1e\x84\xf3\xdd\x03\x14\xc0\xf7\xb2\x29\x1e\x5b\x20\x0a\xc8\xc7\xc3\xb9\x25",
     "Approval", 3, 3, ERC20_APPROVAL_PARAMS},

    // Uniswap V2 Mint (2 topics: sig + sender, amounts in data)
    {"\x4c\x20\x9b\x5f\xc8\xad\x50\x75\x8f\x13\xe2\xe1\x08\x8b\xa5\x6a\x56\x0d\xff\x69\x0a\x1c\x6f\xef\x26\x39\x4f\x4c\x03\x82\x1c\x4f",
     "Mint", 3, 2, UNISWAP_MINT_PARAMS},

    // Uniswap V2 Burn (3 topics: sig + sender + to, amounts in data)
    {"\xdc\xcd\x41\x2f\x0b\x12\x52\x81\x9c\xb1\xfd\x33\x0b\x93\x22\x4c\xa4\x26\x12\x89\x2b\xb3\xf4\xf7\x89\x97\x6e\x6d\x81\x93\x64\x96",
     "Burn", 4, 3, UNISWAP_BURN_PARAMS},

    // Uniswap V2 Swap (3 topics: sig + sender + to, 4 amounts in data)
    {"\xd7\x8a\xd9\x5f\xa4\x6c\x99\x4b\x65\x51\xd0\xda\x85\xfc\x27\x5f\xe6\x13\xce\x37\x65\x7f\xb8\xd5\xe3\xd1\x30\x84\x01\x59\xd8\x22",
     "Swap", 6, 3, UNISWAP_SWAP_PARAMS},

    // Uniswap V2 Sync (1 topic: sig only, reserves in data)
    {"\x1c\x41\x1e\x9a\x96\xe0\x71\x24\x1c\x2f\x21\xf7\x72\x6b\x17\xae\x89\xe3\xca\xb4\xc7\x8b\xe5\x0e\x06\x2b\x03\xa9\xff\xfb\xba\xd1",
     "Sync", 2, 1, UNISWAP_SYNC_PARAMS},

    // WETH Deposit (2 topics: sig + dst, wad in data)
    {"\xe1\xff\xfc\xc4\x92\x3d\x04\xb5\x59\xf4\xd2\x9a\x8b\xfc\x6c\xda\x04\xeb\x5b\x0d\x3c\x46\x07\x51\xc2\x40\x2c\x5c\x5c\xc9\x10\x9c",
     "Deposit", 2, 2, WETH_DEPOSIT_PARAMS},

    // WETH Withdrawal (2 topics: sig + src, wad in data)
    {"\x7f\xcf\x53\x2c\x15\xf0\xa6\xdb\x0b\xd6\xd0\xe0\x38\xbe\xa7\x1d\x30\xd8\x08\xc7\xd9\x8c\xb3\xbf\x72\x68\xa9\x5b\xf5\x08\x1b\x65",
     "Withdrawal", 2, 2, WETH_WITHDRAWAL_PARAMS},
};

#define KNOWN_EVENTS_COUNT (sizeof(KNOWN_EVENTS) / sizeof(KNOWN_EVENTS[0]))

/**
 * Tries to decode a known event from the emitted log.
 *
 * Matches `log->topics[0]` and `log->topics_count` against a static table of
 * well-known event signatures. On match, builds decoded `InputParam` entries
 * into the provided `inputs_builder`.
 *
 * Values are encoded as hex strings:
 * - `address`: `"0x"` + 40 hex chars (20 bytes from topic offset 12)
 * - `uint*`: `"0x"` + hex without leading zeros (32 bytes from ABI slot)
 *
 * @param log the emitted log to decode
 * @param inputs_builder SSZ list builder (initialized for the `inputs` field) to populate
 * @return the event name on success, or NULL if the event is not recognized
 */
const char* eth_decode_known_event(const emitted_log_t* log, ssz_builder_t* inputs_builder) {
  if (!log || log->topics_count == 0) return NULL;

  const known_event_t* event = NULL;
  for (size_t i = 0; i < KNOWN_EVENTS_COUNT; i++) {
    if (log->topics_count == KNOWN_EVENTS[i].expected_topics &&
        memcmp(log->topics[0], KNOWN_EVENTS[i].hash, 32) == 0) {
      event = &KNOWN_EVENTS[i];
      break;
    }
  }
  if (!event) return NULL;

  uint8_t topic_idx   = 1; // topics[0] is the signature hash, indexed params start at 1
  size_t  data_offset = 0; // non-indexed params are read from data in 32-byte ABI slots

  for (uint8_t i = 0; i < event->param_count; i++) {
    const event_param_def_t* param = &event->params[i];

    // Build a single InputParam container
    ssz_builder_t param_builder = ssz_builder_for_def(inputs_builder->def->def.vector.type);

    // name
    ssz_add_bytes(&param_builder, "name", bytes((uint8_t*) param->name, strlen(param->name)));
    // type
    ssz_add_bytes(&param_builder, "type", bytes((uint8_t*) param->type, strlen(param->type)));

    // value - format as hex string
    char     tmp[68]; // "0x" + max 64 hex chars + null
    uint8_t* src;

    if (param->indexed) {
      src = log->topics[topic_idx++];
    }
    else {
      src = log->data.data + data_offset;
      data_offset += 32;
    }

    if (strcmp(param->type, "bool") == 0) {
      // bool: ABI-encoded as uint8 in 32-byte slot, value in last byte
      sbprintf(tmp, "%s", src[31] ? "true" : "false");
    }
    else if (strcmp(param->type, "address") == 0) {
      // address: last 20 bytes of 32-byte slot
      sbprintf(tmp, "0x%x", bytes(src + 12, 20));
    }
    else if (strncmp(param->type, "bytes", 5) == 0 && param->type[5] >= '1' && param->type[5] <= '9') {
      // bytesN (bytes1..bytes32): first N bytes of 32-byte slot
      uint8_t n = (uint8_t) atoi(param->type + 5);
      if (n > 32) n = 32;
      sbprintf(tmp, "0x%x", bytes(src, n));
    }
    else {
      // uint* / int*: full 32-byte ABI slot without leading zeros
      sbprintf(tmp, "0x%u", bytes(src, 32));
    }

    ssz_add_bytes(&param_builder, "value", bytes((uint8_t*) tmp, strlen(tmp)));
    ssz_add_dynamic_list_builders(inputs_builder, event->param_count, param_builder);
  }

  return event->name;
}
