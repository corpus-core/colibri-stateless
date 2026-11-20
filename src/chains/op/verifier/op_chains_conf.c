// op_chains_conf.c - Centralized OP Stack chain configurations implementation
#include "op_chains_conf.h"
#include "bytes.h"
#include "json.h"
#include "state.h"
#include <stddef.h>
#include <string.h>

// Centralized OP Stack chain configurations.
// Real entries store SystemConfig.unsafeBlockSigner() from Ethereum mainnet.
// Placeholder entries below are not verifiable until those signers are replaced.
static const op_chain_config_t op_chain_configs[] = {
    // Well-known OP Stack chains with verified sequencer addresses
    OP_CHAIN_CONFIG(
        10,
        "\xAA\xAA\x45\xd9\x54\x9E\xDA\x09\xE7\x09\x37\x01\x35\x20\x21\x43\x82\xFf\xc4\xA2",
        "OP Mainnet",
        "https://op-mainnet.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        8453,
        "\xAf\x6E\x19\xBE\x0F\x9c\xE7\xf8\xaf\xd4\x9a\x18\x24\x85\x10\x23\xA8\x24\x9e\x8a",
        "Base",
        "https://base.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        480,
        "\x22\x70\xd6\xeC\x8E\x76\x0d\xaA\x31\x7D\xD9\x78\xcF\xB9\x8C\x8f\x14\x4B\x1f\x3A",
        "Worldchain",
        "https://worldchain.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        7777777,
        "\x3D\xc8\xDf\xd0\x70\x9C\x83\x5c\xAd\x15\xa6\xA2\x7e\x08\x9F\xF4\xcF\x4C\x92\x28",
        "Zora",
        "https://zora.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        130,
        "\xFa\xAC\xA0\xA1\x0e\x4a\x23\xC5\xE0\x14\x85\xDE\x07\x76\x75\x47\xd4\xD8\x05\x6D",
        "Unichain",
        "https://unichain.operationsolarstorm.org/latest",
        3),
    // No operationsolarstorm endpoint. Gossip does not need one; http_endpoint stays NULL.
    OP_CHAIN_CONFIG(
        57073,
        "\x7b\x32\x22\x82\xDF\x45\xE5\x37\xE5\xde\x76\xD6\x0e\x14\x32\xDb\x3c\xF3\xF8\xE1",
        "Ink",
        NULL,
        3),

    // Additional OP Stack chains (TODO: Update with actual sequencer addresses)
    OP_CHAIN_CONFIG(
        424,
        "\x99\x19\x9F\x2c\x2A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "PGN (Public Goods Network)",
        "https://pgn.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        291,
        "\x88\x18\x8F\x3c\x3A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "Orderly Network",
        "https://orderly.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        34443,
        "\x77\x17\x7E\x2c\x2A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "Mode Network",
        "https://mode.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        252,
        "\x66\x16\x6D\x1c\x1A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "Fraxtal",
        "https://fraxtal.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        5000,
        "\x55\x15\x5C\x0c\x0A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "Mantle",
        "https://mantle.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        8217,
        "\x44\x14\x4B\x9b\x9A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "Klaytn",
        "https://klaytn.operationsolarstorm.org/latest",
        3),
};

// Additional OP Stack chains registered at runtime through `c4_op_register_chain`.
// Built-in entries in `op_chain_configs[]` always take precedence: the lookup
// below scans them first, so a plugin cannot override a known chain.
#define OP_CHAIN_CONFIG_EXTRA_MAX 8

static op_chain_config_t op_chain_configs_extra[OP_CHAIN_CONFIG_EXTRA_MAX];
static uint32_t          op_chain_configs_extra_len = 0;

// Get chain configuration by chain ID
const op_chain_config_t* op_get_chain_config(uint64_t chain_id) {
  for (size_t i = 0; i < sizeof(op_chain_configs) / sizeof(op_chain_configs[0]); i++) {
    if (op_chain_configs[i].chain_id == chain_id) {
      return &op_chain_configs[i];
    }
  }
  for (uint32_t i = 0; i < op_chain_configs_extra_len; i++) {
    if (op_chain_configs_extra[i].chain_id == chain_id) {
      return &op_chain_configs_extra[i];
    }
  }
  return NULL; // Chain not found
}

bool c4_op_register_chain(c4_state_t* state, json_t data) {
  if (!state) return false;
  // `chain_id` accepts both JSON numbers and hex-strings so wallet-style
  // configs (`"0x5a2"`) and plain JSON numbers (`1442`) both work.
  const char* schema_err = json_validate(
      data,
      "{chain_id:uint|hexuint,sequencer_address:address}",
      "c4_op_register_chain");
  if (schema_err) {
    c4_state_add_error(state, schema_err);
    safe_free((void*) schema_err);
    return false;
  }

  uint64_t chain_id = json_get_uint64(data, "chain_id");
  if (chain_id == 0) {
    c4_state_add_error(state, "c4_op_register_chain: chain_id must not be 0");
    return false;
  }
  if (op_get_chain_config(chain_id)) {
    c4_state_add_error(state, "c4_op_register_chain: chain_id is already registered");
    return false;
  }
  if (op_chain_configs_extra_len >= OP_CHAIN_CONFIG_EXTRA_MAX) {
    c4_state_add_error(state, "c4_op_register_chain: extra chain config table is full");
    return false;
  }

  buffer_t buf    = {0};
  bytes_t  parsed = json_as_bytes(json_get(data, "sequencer_address"), &buf);
  if (!parsed.data || parsed.len != 20) {
    buffer_free(&buf);
    c4_state_add_error(state, "c4_op_register_chain: sequencer_address must be 20 bytes");
    return false;
  }

  // The 20-byte sequencer address is stored as a heap-allocated raw byte
  // string owned by the registry. Built-in entries use string literals; both
  // are valid 20-byte buffers for `sequencer_address`.
  uint8_t* addr = safe_malloc(20);
  memcpy(addr, parsed.data, 20);
  buffer_free(&buf);

  op_chain_config_t* slot = &op_chain_configs_extra[op_chain_configs_extra_len];
  memset(slot, 0, sizeof(*slot));
  slot->chain_id          = chain_id;
  slot->sequencer_address = (const char*) addr;
  op_chain_configs_extra_len++;
  return true;
}

void c4_op_reset_chain_config_registry(void) {
  for (uint32_t i = 0; i < op_chain_configs_extra_len; i++) {
    safe_free((void*) op_chain_configs_extra[i].sequencer_address);
    memset(&op_chain_configs_extra[i], 0, sizeof(op_chain_configs_extra[i]));
  }
  op_chain_configs_extra_len = 0;
}

// Get number of supported chains
size_t op_get_supported_chains_count(void) {
  return sizeof(op_chain_configs) / sizeof(op_chain_configs[0]);
}

// Get all supported chain configurations
const op_chain_config_t* op_get_all_chain_configs(void) {
  return op_chain_configs;
}
