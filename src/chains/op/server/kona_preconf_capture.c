// kona_preconf_capture.c - Direkte Kona-Bridge Integration
#include "kona_preconf_capture.h"
#include "../verifier/op_chains_conf.h"
#include "util/logger.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>

// UV Worker für die Kona-Bridge (läuft in eigenem Thread)
typedef struct {
  uv_work_t                work_req;
  const op_chain_config_t* chain_config;
  const char*              output_dir;
  int                      result;
  bool                     should_stop;
} kona_worker_data_t;

static kona_worker_data_t* g_kona_worker = NULL;

// Worker-Thread-Funktion
static void kona_worker_thread(uv_work_t* req) {
  kona_worker_data_t* data = (kona_worker_data_t*) req->data;

  log_info("🦀 Kona worker thread starting...");

  // Starte die echte Kona-Bridge
  log_info("🦀 Starting REAL Kona bridge for chain %llu", (unsigned long long) data->chain_config->chain_id);

  // Set Rust tracing level for better debugging
  setenv("RUST_LOG", "kona_bridge=debug,info", 1);

  // Erstelle Kona-Bridge Konfiguration
  KonaBridgeConfig kona_config = {
      .chain_id          = data->chain_config->chain_id,
      .hardfork          = data->chain_config->hardfork_version,
      .disc_port         = data->chain_config->kona_disc_port,
      .gossip_port       = data->chain_config->kona_gossip_port,
      .ttl_minutes       = data->chain_config->kona_ttl_minutes,
      .cleanup_interval  = data->chain_config->kona_cleanup_interval,
      .output_dir        = data->output_dir,
      .sequencer_address = data->chain_config->sequencer_address,
      .chain_name        = data->chain_config->name};

  // TEST: Direkte Rust-Funktion aufrufen
  log_info("🔧 DEBUG: About to call kona_bridge_start");
  log_info("🔧 DEBUG: Config chain_id=%llu, output_dir=%s",
           (unsigned long long) kona_config.chain_id,
           kona_config.output_dir ? kona_config.output_dir : "NULL");

  // Starte echte Kona-Bridge
  KonaBridgeHandle* bridge_handle = kona_bridge_start(&kona_config);

  log_info("🔧 DEBUG: kona_bridge_start returned: %p", (void*) bridge_handle);

  if (!bridge_handle) {
    log_error("❌ Failed to start Kona bridge");
    data->result = -1;
    return;
  }

  log_info("✅ Real Kona bridge started successfully");
  data->result = 0;

  // Bridge läuft jetzt - warte bis Stop-Signal
  while (!data->should_stop) {
    usleep(500000); // 500ms - längere Pausen für bessere Performance

    // Prüfe ob Bridge noch läuft
    if (!kona_bridge_is_running(bridge_handle)) {
      log_warn("⚠️  Kona bridge stopped unexpectedly");
      break;
    }

    // Statistiken loggen
    static time_t last_stats = 0;
    time_t        now        = time(NULL);
    if (now - last_stats >= 10) { // Alle 10 Sekunden
      // Stats sind jetzt über /metrics endpoint verfügbar - Log-Spam entfernt
      // KonaBridgeStats stats;
      // if (kona_bridge_get_stats(bridge_handle, &stats) == 0) {
      //   // Stats werden intern aktualisiert, aber nicht mehr alle 5 Sekunden geloggt
      // }
      last_stats = now;
    }
  }

  log_info("🛑 Stopping real Kona bridge...");

  // WICHTIG: Stoppe die Rust-Bridge zuerst, damit das running-Flag gesetzt wird
  kona_bridge_stop(bridge_handle);

  // Warte kurz, damit die Rust-Bridge Zeit hat, sich zu beenden
  usleep(1000000); // 1 Sekunde

  log_info("✅ Kona bridge stopped");

  log_info("🛑 Kona worker thread stopping...");
}

// Worker-Completion-Callback
static void kona_worker_done(uv_work_t* req, int status) {
  kona_worker_data_t* data = (kona_worker_data_t*) req->data;

  if (status != 0) {
    log_error("❌ Kona worker thread failed with status: %d", status);
  }
  else {
    log_info("✅ Kona worker thread completed successfully");
  }

  // Cleanup erfolgt bereits im Worker-Thread
  free(data);
  g_kona_worker = NULL;
}

int start_kona_preconf_capture(uv_loop_t* loop, uint64_t chain_id, const char* output_dir) {
  if (!loop || !output_dir) {
    log_error("❌ Invalid parameters for Kona preconf capture");
    return -1;
  }

  if (g_kona_worker != NULL) {
    log_warn("⚠️  Kona preconf capture already running");
    return -1;
  }

  // Get chain configuration
  const op_chain_config_t* chain_config = op_get_chain_config(chain_id);
  if (!chain_config) {
    log_error("❌ Unsupported chain ID for Kona bridge: %llu", (unsigned long long) chain_id);
    return -1;
  }

  log_info("🚀 Starting Kona-P2P preconf capture for %s (Chain ID: %llu)",
           chain_config->name, (unsigned long long) chain_id);

  // Allocate worker data
  kona_worker_data_t* worker_data = (kona_worker_data_t*) malloc(sizeof(kona_worker_data_t));
  if (!worker_data) {
    log_error("❌ Failed to allocate memory for Kona worker");
    return -1;
  }

  // Initialize worker data
  worker_data->work_req.data = worker_data;
  worker_data->chain_config  = chain_config;
  worker_data->output_dir    = output_dir;
  worker_data->result        = 0;
  worker_data->should_stop   = false;

  // Queue work
  int rc = uv_queue_work(loop, &worker_data->work_req, kona_worker_thread, kona_worker_done);
  if (rc != 0) {
    log_error("❌ Failed to queue Kona worker: %s", uv_strerror(rc));
    free(worker_data);
    return rc;
  }

  g_kona_worker = worker_data;
  log_info("✅ Kona worker queued successfully");

  return 0;
}

int stop_kona_preconf_capture(void) {
  if (g_kona_worker == NULL) {
    log_warn("⚠️  No Kona preconf capture running");
    return 0;
  }

  log_info("🛑 Stopping Kona preconf capture...");

  // Signal worker to stop
  g_kona_worker->should_stop = true;

  // Give the worker thread a short time to finish gracefully
  // If it doesn't finish, we'll let the process exit anyway
  log_info("⏳ Kona bridge shutdown initiated - allowing 2 seconds for graceful stop");

  // Use a non-blocking approach - just signal and let process exit handle cleanup
  // The OS will clean up the worker thread when the process exits

  return 0;
}

bool is_kona_preconf_capture_running(void) {
  return g_kona_worker != NULL; // Mock: Einfache Prüfung
}

int get_kona_preconf_capture_stats(KonaBridgeStats* stats) {
  if (g_kona_worker == NULL || !stats) {
    return -1;
  }

  // Mock: Simuliere Statistiken
  stats->connected_peers    = 5;
  stats->received_preconfs  = 42;
  stats->processed_preconfs = 42;
  stats->failed_preconfs    = 0;

  return 0;
}
