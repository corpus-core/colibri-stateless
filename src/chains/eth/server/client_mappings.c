#include "eth_clients.h"
#include "handler.h"

// Complete mapping of client types, now specific to the Ethereum chain handler.
static const client_type_mapping_t eth_client_type_mappings[] = {
    {"NIMBUS", "Nimbus", BEACON_CLIENT_NIMBUS},
    {"LODESTAR", "Lodestar", BEACON_CLIENT_LODESTAR},
    {"PRYSM", "Prysm", BEACON_CLIENT_PRYSM},
    {"LIGHTHOUSE", "Lighthouse", BEACON_CLIENT_LIGHTHOUSE},
    {"TEKU", "Teku", BEACON_CLIENT_TEKU},
    {"GRANDINE", "Grandine", BEACON_CLIENT_GRANDINE},
    {"GETH", "Geth", RPC_CLIENT_GETH},
    {"NETHERMIND", "Nethermind", RPC_CLIENT_NETHERMIND},
    {"ERIGON", "Erigon", RPC_CLIENT_ERIGON},
    {"BESU", "Besu", RPC_CLIENT_BESU},
    {NULL, NULL, 0} // Terminator
};

/**
 * @brief Returns the chain-specific client type mappings.
 * This is the implementation for the handler interface.
 */
const client_type_mapping_t* eth_get_client_mappings(http_server_t* server) {
  ETH_HANDLER_CHECK_RETURN(server, NULL);
  // For now, we assume if this handler is compiled, it's for an ETH chain.
  // A check against http_server.chain_id could be added if multiple chain handlers
  // could be active simultaneously for different chain types.
  return eth_client_type_mappings;
}
