#include "handler.h"
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"

// Forward declarations for handlers from the moved files
// These will be registered with the main server.
bool c4_handle_lcu(client_t* client);
bool c4_proxy(client_t* client);

// Forward declarations for background services
void c4_watch_beacon_events();
void c4_stop_beacon_watcher();

/**
 * @brief Initializes the Ethereum-specific parts of the server.
 *
 * This function is called by the generic server during startup.
 * It checks if the configured chain_id is for Ethereum and, if so,
 * registers Ethereum-specific HTTP handlers and starts background services.
 */
void eth_server_init(http_server_t* server) {
  ETH_HANDLER_CHECK(server);

  log_info("Initializing Ethereum server handlers...");

  // Register handlers that are now chain-specific
  c4_register_http_handler(c4_handle_lcu);
  c4_register_http_handler(c4_proxy);

  // internal handlers
  c4_register_internal_handler(c4_handle_period_store);
  c4_register_internal_handler(c4_handle_lcu_updates);

  // Start background services like the beacon event watcher if configured
  if (eth_config.stream_beacon_events) {
    log_info("Starting beacon event watcher...");
    c4_watch_beacon_events();
  }
}

/**
 * @brief Shuts down the Ethereum-specific parts of the server.
 *
 * This function is called by the generic server during shutdown.
 * It's responsible for cleanly stopping any background services
 * that were started by this handler.
 */
void eth_server_shutdown(http_server_t* server) {
  ETH_HANDLER_CHECK(server);

  // Stop background services if they were configured to run
  if (eth_config.stream_beacon_events) {
    log_info("Stopping beacon event watcher...");
    c4_stop_beacon_watcher();
  }
}
