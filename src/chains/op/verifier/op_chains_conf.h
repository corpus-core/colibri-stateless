// op_chains_conf.h - Centralized OP Stack chain configurations
#pragma once

#include "chains.h"
#include "json.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OP Stack chain configuration structure
typedef struct {
  uint64_t    chain_id;
  const char* sequencer_address; // 20 bytes hex string (always available)

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
  uint32_t kona_http_failure_threshold; // 0 = gossip-only; >0 = HTTP-first then gossip (default: 0)
#endif
} op_chain_config_t;

// Macro to define chain configurations conditionally
#ifdef PROVER
#define OP_CHAIN_CONFIG(id, signer, chain_name, endpoint, hf) \
  {.chain_id = (id), .sequencer_address = (signer), .name = (chain_name), .http_endpoint = (endpoint), .hardfork_version = (hf), .kona_disc_port = 9090, .kona_gossip_port = 9091, .kona_ttl_minutes = 60, .kona_cleanup_interval = 5, .kona_http_poll_interval = 1, .kona_http_failure_threshold = 0}
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

/**
 * Registers an additional OP Stack chain at runtime from a JSON description.
 *
 * Called via the generated `c4_register_chain(state, C4_CHAIN_TYPE_OP, data)`
 * dispatcher. Reference JSON layout:
 *
 * ```json
 * {
 *   "chain_id":          "0x5a2",
 *   "sequencer_address": "0xAAAA45d9549EDA09E70937013520214382Ffc4A2"
 * }
 * ```
 *
 * `chain_id` accepts both a plain JSON number (`1442`) and an Ethereum-style
 * hex string (`"0x5a2"`). Only the verifier-facing `chain_id` and
 * `sequencer_address` fields are populated; the prover-specific fields of
 * `op_chain_config_t` (Kona-Bridge settings, HTTP endpoint) remain at zero.
 * Built-in chains always take precedence over runtime entries.
 *
 * @param state state collecting the error message on failure (must not be NULL)
 * @param data parsed JSON body with one chain description
 * @return true on success, false with `state->error` populated on failure
 */
bool c4_op_register_chain(c4_state_t* state, json_t data);

/**
 * Resets the runtime-registered OP Stack chain configs.
 *
 * Only intended for tests: frees every heap-allocated sequencer address and
 * empties the extras array. Built-ins are unaffected.
 */
void c4_op_reset_chain_config_registry(void);

#ifdef __cplusplus
}
#endif
