/*
 * integration.c - Integration der Kona-Bridge in das C-Server-System
 *
 * Diese Datei zeigt, wie die Kona-Bridge in das bestehende colibri-stateless
 * Server-System integriert werden kann.
 */

#include "../server/server.h"
#include "../util/log.h"
#include "../verifier/op_chains_conf.h"
#include "kona_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Globale Bridge-Instanz
static KonaBridgeHandle* g_kona_bridge = NULL;

/**
 * Initialisiert und startet die Kona-Bridge basierend auf zentraler Chain-Konfiguration
 */
int start_kona_bridge_from_config(const op_chain_config_t* chain_config, const char* output_dir) {
  if (!chain_config) {
    log_error("No chain configuration provided");
    return -1;
  }

  if (g_kona_bridge != NULL) {
    log_warn("Kona bridge is already running");
    return -1;
  }

  // Initialisiere Rust-Logging (einmal)
  static int logging_initialized = 0;
  if (!logging_initialized) {
    kona_bridge_init_logging();
    logging_initialized = 1;
  }

  // Konfiguriere die Bridge aus zentraler Chain-Config
  KonaBridgeConfig config = {
      .chain_id          = chain_config->chain_id,
      .hardfork          = chain_config->hardfork_version,
      .disc_port         = chain_config->kona_disc_port,
      .gossip_port       = chain_config->kona_gossip_port,
      .ttl_minutes       = chain_config->kona_ttl_minutes,
      .cleanup_interval  = chain_config->kona_cleanup_interval,
      .output_dir        = output_dir,
      .sequencer_address = chain_config->sequencer_address,
      .chain_name        = chain_config->name};

  log_info("Starting Kona-P2P bridge for %s (Chain ID: %llu)",
           chain_config->name, (unsigned long long) chain_config->chain_id);
  log_info("Output directory: %s", output_dir ? output_dir : "default");
  log_info("Expected sequencer: %s", chain_config->sequencer_address);
  log_info("Discovery: %u, Gossip: %u", config.disc_port, config.gossip_port);

  // Starte die Bridge
  g_kona_bridge = kona_bridge_start(&config);
  if (g_kona_bridge == NULL) {
    log_error("Failed to start Kona bridge");
    return -1;
  }

  log_info("✅ Kona-P2P bridge started successfully");
  return 0;
}

/**
 * Legacy-Wrapper für Rückwärtskompatibilität
 */
int start_kona_bridge(uint32_t chain_id, const char* output_dir,
                      const char* sequencer_address, const char* chain_name) {
  const op_chain_config_t* chain_config = op_get_chain_config(chain_id);
  if (!chain_config) {
    log_error("Unsupported chain ID: %u", chain_id);
    return -1;
  }

  return start_kona_bridge_from_config(chain_config, output_dir);
}

/**
 * Stoppt die Kona-Bridge
 */
void stop_kona_bridge(void) {
  if (g_kona_bridge == NULL) {
    return;
  }

  log_info("Stopping Kona-P2P bridge...");
  kona_bridge_stop(g_kona_bridge);
  g_kona_bridge = NULL;
  log_info("✅ Kona-P2P bridge stopped");
}

/**
 * Prüft den Status der Bridge
 */
int is_kona_bridge_running(void) {
  if (g_kona_bridge == NULL) {
    return 0;
  }
  return kona_bridge_is_running(g_kona_bridge);
}

/**
 * Gibt Bridge-Statistiken zurück
 */
int get_kona_bridge_stats(KonaBridgeStats* stats) {
  if (g_kona_bridge == NULL || stats == NULL) {
    return -1;
  }
  return kona_bridge_get_stats(g_kona_bridge, stats);
}

/**
 * Beispiel für Integration in den Server-Startup-Code
 */
void example_server_integration(void) {
  // Diese Funktion zeigt, wie die Bridge in das Server-System integriert wird

  // 1. Bestimme Chain-Konfiguration (aus Server-Config)
  uint32_t    chain_id          = 8453; // Base als Beispiel
  const char* output_dir        = "./preconfs";
  const char* sequencer_address = "0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a";
  const char* chain_name        = "Base";

  // 2. Starte Kona-Bridge
  if (start_kona_bridge(chain_id, output_dir, sequencer_address, chain_name) != 0) {
    log_error("Failed to start Kona bridge - falling back to HTTP mode");
    // Fallback zur Go-Bridge oder HTTP-only Modus
    return;
  }

  // 3. Optional: Statistiken periodisch loggen
  KonaBridgeStats stats;
  if (get_kona_bridge_stats(&stats) == 0) {
    log_info("Bridge stats: %u peers, %u preconfs received, %u processed",
             stats.connected_peers, stats.received_preconfs, stats.processed_preconfs);
  }

  // 4. Bei Server-Shutdown: Bridge stoppen
  // stop_kona_bridge(); // <- In server shutdown handler aufrufen
}

/**
 * Signal-Handler für graceful shutdown
 */
void kona_bridge_signal_handler(int signal) {
  log_info("Received signal %d, shutting down Kona bridge...", signal);
  stop_kona_bridge();
}

/**
 * Erweiterte Konfiguration basierend auf Chain-ID
 */
KonaBridgeConfig create_bridge_config_for_chain(uint32_t chain_id, const char* output_dir) {
  KonaBridgeConfig config = {
      .chain_id          = chain_id,
      .hardfork          = 4,
      .disc_port         = 9090,
      .gossip_port       = 9091,
      .ttl_minutes       = 30,
      .cleanup_interval  = 5,
      .output_dir        = output_dir,
      .sequencer_address = NULL,
      .chain_name        = NULL};

  // Chain-spezifische Konfiguration
  switch (chain_id) {
    case 10: // OP Mainnet
      config.sequencer_address = "0xAAAA45d9549EDA09E70937013520214382Ffc4A2";
      config.chain_name        = "OP Mainnet";
      break;

    case 8453: // Base
      config.sequencer_address = "0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a";
      config.chain_name        = "Base";
      break;

    case 130: // Unichain
      config.sequencer_address = "0x833C6f278474A78658af91aE8edC926FE33a230e";
      config.chain_name        = "Unichain";
      break;

    default:
      log_warn("Unknown chain ID %u - using default configuration", chain_id);
      break;
  }

  return config;
}
