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

  // Kona-Bridge spezifische Konfiguration
  uint32_t kona_disc_port;        // Discovery Port (default: 9090)
  uint32_t kona_gossip_port;      // Gossip Port (default: 9091)
  uint32_t kona_ttl_minutes;      // TTL für Preconfs (default: 30)
  uint32_t kona_cleanup_interval; // Cleanup Intervall (default: 5)
#endif
} op_chain_config_t;

// Macro to define chain configurations conditionally
#ifdef PROOFER
#define OP_CHAIN_CONFIG(id, signer, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer), .name = (chain_name), .http_endpoint = (endpoint), .hardfork_version = (hf), .kona_disc_port = 9090, .kona_gossip_port = 9091, .kona_ttl_minutes = 30, .kona_cleanup_interval = 5}
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
