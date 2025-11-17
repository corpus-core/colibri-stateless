#ifndef ETH_SERVER_HANDLER_H
#define ETH_SERVER_HANDLER_H

#include "server/server.h"
#include "util/json.h"

// Helper macro to check if this handler should be active for the given server
#define ETH_HANDLER_CHECK(server)                                                 \
  do {                                                                            \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_ETHEREUM) \
      return;                                                                     \
  } while (0)

// Helper macro for functions that return a value
#define ETH_HANDLER_CHECK_RETURN(server, default_return)                          \
  do {                                                                            \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_ETHEREUM) \
      return (default_return);                                                    \
  } while (0)

// Ethereum-specific HTTP handlers, now part of the eth module
bool c4_proxy(client_t* client);
bool c4_handle_lcu(client_t* client);
bool c4_handle_lcu_updates(single_request_t* r);

// Ethereum-specific background service functions
void c4_handle_new_head(json_t head);
void c4_handle_finalized_checkpoint(json_t checkpoint);
void c4_watch_beacon_events();
void c4_stop_beacon_watcher();

#ifdef TEST
// Test helpers for beacon watcher
void c4_test_set_beacon_watcher_url(const char* url);
void c4_test_set_beacon_watcher_no_reconnect(bool disable);
void c4_watch_beacon_events(void);
void c4_stop_beacon_watcher(void);
bool c4_beacon_watcher_is_running(void);
#endif

#endif // ETH_SERVER_HANDLER_H
