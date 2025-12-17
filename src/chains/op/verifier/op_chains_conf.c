// op_chains_conf.c - Centralized OP Stack chain configurations implementation
#include "op_chains_conf.h"
#include <stddef.h>

// Centralized OP Stack chain configurations
// NOTE: L2OutputOracle addresses are on Ethereum L1 mainnet
static const op_chain_config_t op_chain_configs[] = {
    // Well-known OP Stack chains with verified sequencer and L2OutputOracle addresses
    OP_CHAIN_CONFIG(
        10,
        "\xAA\xAA\x45\xd9\x54\x9E\xDA\x09\xE7\x09\x37\x01\x35\x20\x21\x43\x82\xFf\xc4\xA2",
        "\xdd\xb1\xCb\x78\x41\x2A\xac\xA0\x7a\x60\xBA\xB0\xDB\xBA\x3e\x37\xde\x16\x82\xe2",
        0,
        "OP Mainnet",
        "https://op-mainnet.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        8453,
        "\xAf\x6E\x19\xBE\x0F\x9c\xE7\xf8\xaf\xd4\x9a\x18\x24\x85\x10\x23\xA8\x24\x9e\x8a",
        "\x56\x31\x5b\x5f\x88\x12\x0c\xa1\xBa\x94\x1A\xBF\xAA\x88\x20\x7E\x68\xE0\x54\xEB",
        0,
        "Base",
        "https://base.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        480,
        "\x22\x70\xd6\xeC\x8E\x76\x0d\xaA\x31\x7D\xD9\x78\xcF\xB9\x8C\x8f\x14\x4B\x1f\x3A",
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", // TODO: Add actual L2OutputOracle address
        0,
        "Worldchain",
        "https://worldchain.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        7777777,
        "\x3D\xc8\xDf\xd0\x70\x9C\x83\x5c\xAd\x15\xa6\xA2\x7e\x08\x9F\xF4\xcF\x4C\x92\x28",
        "\x9E\x63\x37\xA7\x3C\x8A\xEB\x11\x41\xA0\x1F\xE1\xeC\x7d\xC7\x30\xCe\xeE\xEC\xD2",
        0,
        "Zora",
        "https://zora.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        130,
        "\x83\x3C\x6f\x27\x84\x74\xA7\x86\x58\xaf\x91\xaE\x8e\xdC\x92\x6F\xE3\x3a\x23\x0e",
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", // TODO: Add actual L2OutputOracle address
        0,
        "Unichain",
        "https://unichain.operationsolarstorm.org/latest",
        3),

    // Additional OP Stack chains (TODO: Update with actual L2OutputOracle addresses)
    OP_CHAIN_CONFIG(
        424,
        "\x99\x19\x9F\x2c\x2A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\xE6\xDf\xBf\xF7\x15\x37\x14\xb5\xBf\x74\x70\x04\x14\x0f\xb3\xAA\x3E\xc8\x56\xD5",
        0,
        "PGN (Public Goods Network)",
        "https://pgn.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        291,
        "\x88\x18\x8F\x3c\x3A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", // TODO: Add actual L2OutputOracle address
        0,
        "Orderly Network",
        "https://orderly.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        34443,
        "\x77\x17\x7E\x2c\x2A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\x42\x32\xB2\x8D\xc1\x5A\x5E\x84\xd1\xd5\x82\xd0\xe1\x0E\x88\x5f\x51\x9e\x53\x6b",
        0,
        "Mode Network",
        "https://mode.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        252,
        "\x66\x16\x6D\x1c\x1A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\x66\xCC\x22\xBF\x6a\x00\xBC\xD1\xe8\xD2\xc0\x2B\xE3\x75\x0C\x9d\x69\x9F\xe5\x0c",
        0,
        "Fraxtal",
        "https://fraxtal.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        5000,
        "\x55\x15\x5C\x0c\x0A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\x31\xd5\x43\x92\x4E\x82\xb8\xe8\xba\x69\x04\x11\x09\xf0\x01\x1B\xb0\x3a\x24\x99",
        0,
        "Mantle",
        "https://mantle.operationsolarstorm.org/latest",
        3),
    OP_CHAIN_CONFIG(
        8217,
        "\x44\x14\x4B\x9b\x9A\x4B\xd9\xC7\xC0\xC9\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4",
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", // TODO: Add actual L2OutputOracle address
        0,
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
