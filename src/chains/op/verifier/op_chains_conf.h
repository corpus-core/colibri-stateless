// op_chains_conf.h - Centralized OP Stack chain configurations
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OP Stack chain configuration structure
typedef struct {
  uint64_t    chain_id;
  const char* sequencer_address; // 20 bytes hex string (always available)

#ifdef PROOFER
  // Additional fields only available when building with PROOFER support
  const char* name;
  const char* http_endpoint;
  int         hardfork_version;
#endif
} op_chain_config_t;

// Macro to define chain configurations conditionally
#ifdef PROOFER
#define OP_CHAIN_CONFIG(id, signer, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer), .name = (chain_name), .http_endpoint = (endpoint), .hardfork_version = (hf)}
#else
#define OP_CHAIN_CONFIG(id, signer, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer)}
#endif

// Function to get chain configuration by chain ID
const op_chain_config_t* op_get_chain_config(uint64_t chain_id);

// Function to get number of supported chains
size_t op_get_supported_chains_count(void);

// Function to get all supported chain configurations
const op_chain_config_t* op_get_all_chain_configs(void);

#ifdef __cplusplus
}
#endif
