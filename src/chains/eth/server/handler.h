#ifndef ETH_SERVER_HANDLER_H
#define ETH_SERVER_HANDLER_H

#include "server/server.h"
#include "util/json.h"

/** No-op return unless `server->chain_id` is Ethereum. */
#define ETH_HANDLER_CHECK(server)                                                 \
  do {                                                                            \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_ETHEREUM) \
      return;                                                                     \
  } while (0)

/** Like `ETH_HANDLER_CHECK` but returns `default_return` for non-Ethereum chains. */
#define ETH_HANDLER_CHECK_RETURN(server, default_return)                          \
  do {                                                                            \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_ETHEREUM) \
      return (default_return);                                                    \
  } while (0)

/** Ethereum chain proxy routes (`/eth/...` passthrough where configured). */
bool c4_proxy(client_t* client);
/** Beacon light-client bootstrap/update HTTP handlers backed by period store. */
bool c4_handle_lcu(client_t* client);
/** Internal handler: fetch LCU SSZ for period store backfill. */
bool c4_handle_lcu_updates(single_request_t* r);
/** Serves checkpoint / historical summary artifacts from period store. */
bool c4_handle_checkpoints(client_t* client);
/** PAP transaction cache HTTP API (testing / tooling). */
bool c4_handle_tx_cache(client_t* client);
/** `GET /proof/...` delegated block proof shortcut. */
bool c4_handle_proof_get_request(client_t* client);

/**
 * Builds the `Cache-Control` header value for a delegated block proof response, based on the
 * block identifier: concrete block numbers/hashes are immutable, tags (`latest`/`safe`/
 * `justified`/`finalized`) get a bounded TTL that mirrors the client-side freshness logic.
 *
 * @param out      destination buffer
 * @param cap      capacity of `out`
 * @param block    the block identifier (tag or `0x`-prefixed number/hash)
 * @param chain_id the chain id (affects block time / epoch length)
 */
void c4_eth_block_cache_control(char* out, size_t cap, const char* block, chain_id_t chain_id);

/** Period store hook: persist block root/header on beacon head event. */
void c4_handle_new_head(json_t head);
/** Period store hook: advance backfill on finalized checkpoint event. */
void c4_handle_finalized_checkpoint(json_t checkpoint);
/** Starts SSE subscription to configured beacon nodes (background). */
void c4_watch_beacon_events();
/** Stops beacon SSE watcher and releases resources. */
void c4_stop_beacon_watcher();

#ifdef TEST
void c4_test_set_beacon_watcher_url(const char* url);
void c4_test_set_beacon_watcher_no_reconnect(bool disable);
void c4_watch_beacon_events(void);
void c4_stop_beacon_watcher(void);
bool c4_beacon_watcher_is_running(void);
#endif

#endif // ETH_SERVER_HANDLER_H
