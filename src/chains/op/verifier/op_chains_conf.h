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
  const char* l2_output_oracle_address; // L2OutputOracle contract address (20 bytes hex)
  uint8_t     l2_outputs_mapping_slot;  // Storage slot (usually 0)

#ifdef PROVER
  // Additional fields only available when building with PROVER support
  const char* name;
  const char* http_endpoint;
  int         hardfork_version;

  // Kona-Bridge spezifische Konfiguration
  uint32_t kona_disc_port;              // Discovery Port (default: 9090)
  uint32_t kona_gossip_port;            // Gossip Port (default: 9091)
  uint32_t kona_ttl_minutes;            // TTL für Preconfs (default: 60)
  uint32_t kona_cleanup_interval;       // Cleanup Intervall (default: 5)
  uint32_t kona_http_poll_interval;     // HTTP-Polling Intervall in Sekunden (default: 1)
  uint32_t kona_http_failure_threshold; // HTTP-Fehler vor Gossip-Umschaltung (default: 5)
#endif
} op_chain_config_t;

// Macro to define chain configurations conditionally
#ifdef PROVER
#define OP_CHAIN_CONFIG(id, signer, oracle_addr, slot, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer), .l2_output_oracle_address = (oracle_addr), .l2_outputs_mapping_slot = (slot), .name = (chain_name), .http_endpoint = (endpoint), .hardfork_version = (hf), .kona_disc_port = 9090, .kona_gossip_port = 9091, .kona_ttl_minutes = 60, .kona_cleanup_interval = 5, .kona_http_poll_interval = 1, .kona_http_failure_threshold = 5}
#else
#define OP_CHAIN_CONFIG(id, signer, oracle_addr, slot, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer), .l2_output_oracle_address = (oracle_addr), .l2_outputs_mapping_slot = (slot)}
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
