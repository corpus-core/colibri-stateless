#include "handler.h"
#include "../verifier/op_chains_conf.h"
#include "kona_preconf_capture.h"
#include "util/bytes.h"
#include "util/logger.h"
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
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

void op_server_metrics(http_server_t* server, buffer_t* data) {
  OP_HANDLER_CHECK(server);

#ifdef KONA_BRIDGE_AVAILABLE
  KonaBridgeStats stats = {0};
  if (get_kona_preconf_capture_stats(&stats) == 0) {
    // OP Stack Preconfirmation Metrics
    bprintf(data, "# HELP colibri_op_preconf_peers Connected peers in the OP preconf network.\n");
    bprintf(data, "# TYPE colibri_op_preconf_peers gauge\n");
    bprintf(data, "colibri_op_preconf_peers{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.connected_peers);

    bprintf(data, "# HELP colibri_op_preconf_received_total Total number of preconfirmations received.\n");
    bprintf(data, "# TYPE colibri_op_preconf_received_total counter\n");
    bprintf(data, "colibri_op_preconf_received_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.received_preconfs);

    bprintf(data, "# HELP colibri_op_preconf_processed_total Total number of preconfirmations successfully processed.\n");
    bprintf(data, "# TYPE colibri_op_preconf_processed_total counter\n");
    bprintf(data, "colibri_op_preconf_processed_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.processed_preconfs);

    bprintf(data, "# HELP colibri_op_preconf_failed_total Total number of preconfirmations that failed processing.\n");
    bprintf(data, "# TYPE colibri_op_preconf_failed_total counter\n");
    bprintf(data, "colibri_op_preconf_failed_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.failed_preconfs);

    // Success rate metric (derived)
    double success_rate = stats.received_preconfs > 0 ? (double) stats.processed_preconfs / stats.received_preconfs : 0.0;
    bprintf(data, "# HELP colibri_op_preconf_success_rate Success rate of preconfirmation processing (0.0-1.0).\n");
    bprintf(data, "# TYPE colibri_op_preconf_success_rate gauge\n");
    // Format success rate manually as string since bprintf doesn't support float
    char success_rate_str[16];
    snprintf(success_rate_str, sizeof(success_rate_str), "%.3f", success_rate);
    bprintf(data, "colibri_op_preconf_success_rate{chain_id=\"%d\"} %s\n", (uint32_t) server->chain_id, success_rate_str);

    // HTTP/Gossip Mode-specific metrics
    bprintf(data, "# HELP colibri_op_preconf_http_received_total Total number of preconfirmations received via HTTP.\n");
    bprintf(data, "# TYPE colibri_op_preconf_http_received_total counter\n");
    bprintf(data, "colibri_op_preconf_http_received_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.http_received);

    bprintf(data, "# HELP colibri_op_preconf_http_processed_total Total number of preconfirmations processed via HTTP.\n");
    bprintf(data, "# TYPE colibri_op_preconf_http_processed_total counter\n");
    bprintf(data, "colibri_op_preconf_http_processed_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.http_processed);

    bprintf(data, "# HELP colibri_op_preconf_gossip_received_total Total number of preconfirmations received via Gossip.\n");
    bprintf(data, "# TYPE colibri_op_preconf_gossip_received_total counter\n");
    bprintf(data, "colibri_op_preconf_gossip_received_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.gossip_received);

    bprintf(data, "# HELP colibri_op_preconf_gossip_processed_total Total number of preconfirmations processed via Gossip.\n");
    bprintf(data, "# TYPE colibri_op_preconf_gossip_processed_total counter\n");
    bprintf(data, "colibri_op_preconf_gossip_processed_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.gossip_processed);

    bprintf(data, "# HELP colibri_op_preconf_mode_switches_total Total number of HTTP to Gossip mode switches.\n");
    bprintf(data, "# TYPE colibri_op_preconf_mode_switches_total counter\n");
    bprintf(data, "colibri_op_preconf_mode_switches_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.mode_switches);

    bprintf(data, "# HELP colibri_op_preconf_current_mode Current mode of preconfirmation reception (0=HTTP, 1=Gossip, 2=HTTP+Gossip).\n");
    bprintf(data, "# TYPE colibri_op_preconf_current_mode gauge\n");
    bprintf(data, "colibri_op_preconf_current_mode{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.current_mode);

    // Gap Metrics - zeigen verpasste Blöcke
    // total_gaps = received - processed (echte verpasste Blöcke)
    uint32_t real_total_gaps = stats.received_preconfs > stats.processed_preconfs ? stats.received_preconfs - stats.processed_preconfs : 0;
    bprintf(data, "# HELP colibri_op_preconf_gaps_total Total number of missed blocks (received but not processed).\n");
    bprintf(data, "# TYPE colibri_op_preconf_gaps_total counter\n");
    bprintf(data, "colibri_op_preconf_gaps_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, real_total_gaps);

    bprintf(data, "# HELP colibri_op_preconf_http_gaps_total Number of blocks missed during HTTP mode.\n");
    bprintf(data, "# TYPE colibri_op_preconf_http_gaps_total counter\n");
    bprintf(data, "colibri_op_preconf_http_gaps_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.http_gaps);

    bprintf(data, "# HELP colibri_op_preconf_gossip_gaps_total Number of blocks missed during Gossip mode.\n");
    bprintf(data, "# TYPE colibri_op_preconf_gossip_gaps_total counter\n");
    bprintf(data, "colibri_op_preconf_gossip_gaps_total{chain_id=\"%d\"} %d\n", (uint32_t) server->chain_id, stats.gossip_gaps);

    bprintf(data, "\n");
  }
#else
  // Kona-Bridge not available - add placeholder metrics
  bprintf(data, "# HELP colibri_op_preconf_peers Connected peers in the OP preconf network.\n");
  bprintf(data, "# TYPE colibri_op_preconf_peers gauge\n");
  bprintf(data, "colibri_op_preconf_peers{chain_id=\"%d\"} 0\n", (uint32_t) server->chain_id);
  bprintf(data, "\n");
#endif
}
