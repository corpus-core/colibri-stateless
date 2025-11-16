// op_chains_conf.c - Centralized OP Stack chain configurations implementation
#include "op_chains_conf.h"
#include <stddef.h>

// Centralized OP Stack chain configurations
// NOTE: Sequencer addresses are placeholders for new chains - update with actual values
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
        "\x83\x3C\x6f\x27\x84\x74\xA7\x86\x58\xaf\x91\xaE\x8e\xdC\x92\x6F\xE3\x3a\x23\x0e",
        "Unichain",
        "https://unichain.operationsolarstorm.org/latest",
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

// Get chain configuration by chain ID
const op_chain_config_t* op_get_chain_config(uint64_t chain_id) {
  for (size_t i = 0; i < sizeof(op_chain_configs) / sizeof(op_chain_configs[0]); i++) {
    if (op_chain_configs[i].chain_id == chain_id) {
      return &op_chain_configs[i];
    }
  }
  return NULL; // Chain not found
}

// Get number of supported chains
size_t op_get_supported_chains_count(void) {
  return sizeof(op_chain_configs) / sizeof(op_chain_configs[0]);
}

// Get all supported chain configurations
const op_chain_config_t* op_get_all_chain_configs(void) {
  return op_chain_configs;
}
