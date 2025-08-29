#include "eth_clients.h"
#include "handler.h"

// Case-insensitive string search helper from the original file
static bool contains_client_name(const char* response, const char* client_name) {
  if (!response || !client_name) return false;

  const char* pos        = response;
  size_t      client_len = strlen(client_name);

  while (*pos) {
    if (strncasecmp(pos, client_name, client_len) == 0) return true;
    pos++;
  }
  return false;
}

/**
 * @brief Provides the endpoint and payload for a client version detection request.
 * This is the chain-specific implementation for the handler interface.
 */
bool eth_get_detection_request(data_request_type_t type, const char** path, const char** rpc_payload) {
  if (type == C4_DATA_TYPE_BEACON_API) {
    *path        = "eth/v1/node/version";
    *rpc_payload = NULL;
    return true;
  }
  if (type == C4_DATA_TYPE_ETH_RPC) {
    *path        = ""; // RPC endpoint is root, we'll use POST with JSON-RPC
    *rpc_payload = "{\"jsonrpc\":\"2.0\",\"method\":\"web3_clientVersion\",\"params\":[],\"id\":1}";
    return true;
  }
  return false;
}

/**
 * @brief Parses a client version response to determine the client type.
 * This is the chain-specific implementation for the handler interface.
 */
beacon_client_type_t eth_parse_version_response(const char* response, data_request_type_t type) {
  if (!response) return BEACON_CLIENT_UNKNOWN;

  if (type == C4_DATA_TYPE_BEACON_API) {
    // Parse beacon API response: {"data":{"version":"Lodestar/v1.8.0/..."}}
    if (contains_client_name(response, "\"Nimbus")) return BEACON_CLIENT_NIMBUS;
    if (contains_client_name(response, "\"Lodestar")) return BEACON_CLIENT_LODESTAR;
    if (contains_client_name(response, "\"Prysm")) return BEACON_CLIENT_PRYSM;
    if (contains_client_name(response, "\"Lighthouse")) return BEACON_CLIENT_LIGHTHOUSE;
    if (contains_client_name(response, "\"teku")) return BEACON_CLIENT_TEKU;
    if (contains_client_name(response, "\"Grandine")) return BEACON_CLIENT_GRANDINE;
  }
  else if (type == C4_DATA_TYPE_ETH_RPC) {
    // Parse RPC response: {"result":"Geth/v1.10.26-stable/..."}
    if (contains_client_name(response, "Geth/")) return RPC_CLIENT_GETH;
    if (contains_client_name(response, "Nethermind/")) return RPC_CLIENT_NETHERMIND;
    if (contains_client_name(response, "Erigon/")) return RPC_CLIENT_ERIGON;
    if (contains_client_name(response, "Besu/")) return RPC_CLIENT_BESU;
  }

  return BEACON_CLIENT_UNKNOWN;
}
