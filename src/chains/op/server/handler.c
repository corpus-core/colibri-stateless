#include "handler.h"
#include "../verifier/op_chains_conf.h"
#include "kona_preconf_capture.h"
#include "util/logger.h"
#include <libgen.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Timer für verzögerte Initialisierung
static uv_timer_t delayed_init_timer;

static void start_preconf_capture(http_server_t* server) {
  uv_loop_t* loop = uv_default_loop();

  log_info("🦀 Starting Kona-P2P bridge (with HTTP fallback support)");

#ifdef KONA_BRIDGE_AVAILABLE
  int kona_result = start_kona_preconf_capture(loop, http_server.chain_id, http_server.preconf_storage_dir);
  if (kona_result == 0) {
    log_info("✅ Kona-P2P bridge started successfully");
    return;
  }
  else {
    log_error("❌ Kona-P2P bridge failed to start");
  }
#else
  log_error("❌ Kona-P2P bridge not available - rebuild with KONA_BRIDGE_AVAILABLE");
#endif
}

void op_server_init(http_server_t* server) {
  OP_HANDLER_CHECK(server);

  log_info("Initializing OP-Stack server handlers...");

  start_preconf_capture(server);
}

void op_server_shutdown(http_server_t* server) {
  OP_HANDLER_CHECK(server);

  log_info("🛑 Shutting down OP server handler...");

  // Stop Kona preconf capture if running
  stop_kona_preconf_capture();

  log_info("✅ OP server handler shutdown complete");
}
