#include "handler.h"
#include "../verifier/op_chains_conf.h"
#include "kona_preconf_capture.h"
#include "op_preconf_capture.h"
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
  // Verwende die Default-Loop - das ist die Loop, die der Server verwendet
  uv_loop_t* loop = uv_default_loop();

  // NEUE KONA-BRIDGE INTEGRATION
  // Versuche zuerst die Kona-Bridge für echte OP-Stack-Kompatibilität
  if (http_server.preconf_use_gossip) {
    log_info("🦀 Attempting to start Kona-P2P bridge (discv5/ENR compatible)");

#ifdef KONA_BRIDGE_AVAILABLE
#include "kona_preconf_capture.h"

    int kona_result = start_kona_preconf_capture(loop, http_server.chain_id, http_server.preconf_storage_dir);
    if (kona_result == 0) {
      log_info("✅ Kona-P2P bridge started successfully - using native OP-Stack protocol");
      return; // Erfolg - keine Fallback-Bridge nötig
    }
    else {
      log_warn("⚠️  Kona-P2P bridge failed to start, falling back to Go bridge");
    }
#else
    log_warn("⚠️  Kona-P2P bridge not available, falling back to Go bridge");
#endif
  }

  // FALLBACK: BESTEHENDE GO-BRIDGE
  log_info("🔄 Starting Go-based bridge as fallback");
  const char* boots[] = {
      // Echte OP Mainnet Bootnodes (aus ethereum-optimism/op-geth)
      // OP Labs Nodes
      "enode://ca2774c3c401325850b2477fd7d0f27911efbf79b1e8b335066516e2bd8c4c9e0ba9696a94b1cb030a88eac582305ff55e905e64fb77fe0edcd70a4e5296d3ec@34.65.175.185:30305",
      "enode://dd751a9ef8912be1bfa7a5e34e2c3785cc5253110bd929f385e07ba7ac19929fb0e0c5d93f77827291f4da02b2232240fbc47ea7ce04c46e333e452f8656b667@34.65.107.0:30305",
      "enode://c5d289b56a77b6a2342ca29956dfd07aadf45364dde8ab20d1dc4efd4d1bc6b4655d902501daea308f4d8950737a4e93a4dfedd17b49cd5760ffd127837ca965@34.65.202.239:30305",

      // Base Nodes (Coinbase)
      "enode://87a32fd13bd596b2ffca97020e31aef4ddcc1bbd4b95bb633d16c1329f654f34049ed240a36b449fda5e5225d70fe40bc667f53c304b71f8e68fc9d448690b51@3.231.138.188:30301",
      "enode://ca21ea8f176adb2e229ce2d700830c844af0ea941a1d8152a9513b966fe525e809c3a6c73a2c18a12b74ed6ec4380edf91662778fe0b79f6a591236e49e176f9@184.72.129.189:30301",
      "enode://acf4507a211ba7c1e52cdf4eef62cdc3c32e7c9c47998954f7ba024026f9a6b2150cd3f0b734d9c78e507ab70d59ba61dfe5c45e1078c7ad0775fb251d7735a2@3.220.145.177:30301",
      "enode://8a5a5006159bf079d06a04e5eceab2a1ce6e0f721875b2a9c96905336219dbe14203d38f70f3754686a6324f786c2f9852d8c0dd3adac2d080f4db35efc678c5@3.231.11.52:30301",
      "enode://cdadbe835308ad3557f9a1de8db411da1a260a98f8421d62da90e71da66e55e98aaa8e90aa7ce01b408a54e4bd2253d701218081ded3dbe5efbbc7b41d7cef79@54.198.153.150:30301",

      // Uniswap Labs Nodes
      "enode://b1a743328188dba3b2ed8c06abbb2688fabe64a3251e43bd77d4e5265bbd5cf03eca8ace4cde8ddb0c49c409b90bf941ebf556094638c6203edd6baa5ef0091b@3.134.214.169:30303",
      "enode://ea9eaaf695facbe53090beb7a5b0411a81459bbf6e6caac151e587ee77120a1b07f3b9f3a9550f797d73d69840a643b775fd1e40344dea11e7660b6a483fe80e@52.14.30.39:30303",
      "enode://77b6b1e72984d5d50e00ae934ffea982902226fe92fa50da42334c2750d8e405b55a5baabeb988c88125368142a64eda5096d0d4522d3b6eef75d166c7d303a9@3.148.100.173:30303",
  };

  // Ermittle den Pfad zur Server-Executable
  char exe_path[PATH_MAX];
  char bridge_path[PATH_MAX];

#ifdef __APPLE__
  // macOS-spezifische Methode
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) != 0) {
    log_error("Failed to get executable path on macOS");
    strcpy(exe_path, "./server"); // Fallback
  }
#elif defined(__linux__)
  // Linux-spezifische Methode
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len == -1) {
    log_error("Failed to get executable path on Linux");
    strcpy(exe_path, "./server"); // Fallback
  }
  else {
    exe_path[len] = '\0';
  }
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  // BSD-spezifische Methode
  ssize_t len = readlink("/proc/curproc/file", exe_path, sizeof(exe_path) - 1);
  if (len == -1) {
    log_error("Failed to get executable path on BSD");
    strcpy(exe_path, "./server"); // Fallback
  }
  else {
    exe_path[len] = '\0';
  }
#else
  // Generischer Fallback
  log_warn("Unknown platform, using relative path fallback");
  strcpy(exe_path, "./server");
#endif

  // Extrahiere das Verzeichnis und füge opg_bridge hinzu
  char exe_path_copy[PATH_MAX];
  strcpy(exe_path_copy, exe_path); // dirname() modifiziert den String
  char* exe_dir = dirname(exe_path_copy);
  snprintf(bridge_path, sizeof(bridge_path), "%s/opg_bridge", exe_dir);

  log_info("Server executable path: %s", exe_path);
  log_info("Bridge path: %s", bridge_path);

  // Get centralized chain configuration
  const op_chain_config_t* chain_config = op_get_chain_config(http_server.chain_id);
  if (!chain_config) {
    log_error("Chain ID %llu not supported in centralized configuration", (unsigned long long) http_server.chain_id);
    return;
  }

  // Convert binary sequencer address to hex string for Go bridge
  static char sequencer_hex[43]; // "0x" + 40 hex chars + null terminator
  snprintf(sequencer_hex, sizeof(sequencer_hex), "0x");
  for (int i = 0; i < 20; i++) {
    snprintf(sequencer_hex + 2 + i * 2, 3, "%02x", ((const uint8_t*) chain_config->sequencer_address)[i]);
  }

  op_chain_config cfg = {
      .chain_id         = http_server.chain_id,
      .hardfork_version = 3,
      .out_dir          = http_server.preconf_storage_dir,
      .bootnodes        = boots,
      .bootnodes_len    = 11,
      .bridge_path      = bridge_path,
      .use_gossip       = http_server.preconf_use_gossip != 0,
      // Pass centralized chain configuration
      .chain_name        = chain_config->name,
      .http_endpoint     = chain_config->http_endpoint,
      .sequencer_address = sequencer_hex,
  };
  op_capture_handle* h  = NULL;
  int                rc = op_preconf_start(loop, &cfg, &h);
  if (rc != 0) {
    log_error("Failed to start preconf capture: %d", rc);
    return;
  }

  //  op_preconf_stop(h);
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
