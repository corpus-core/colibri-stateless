#ifndef ETH_SERVER_HANDLER_H
#define ETH_SERVER_HANDLER_H

#include "server/server.h"
#include "util/json.h"

// Ethereum-specific HTTP handlers, now part of the eth module
bool c4_proxy(client_t* client);
bool c4_handle_lcu(client_t* client);

// Ethereum-specific background service functions
void c4_handle_new_head(json_t head);
void c4_handle_finalized_checkpoint(json_t checkpoint);
void c4_watch_beacon_events();
void c4_stop_beacon_watcher();

#endif // ETH_SERVER_HANDLER_H
