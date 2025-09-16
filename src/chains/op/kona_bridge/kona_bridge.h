/*
 * kona_bridge.h - C Interface für die Kona-P2P OP Stack Bridge
 *
 * Diese Bridge verwendet kona-p2p für echte OP-Stack-Kompatibilität
 * und kann direkt in das bestehende C-Server-System integriert werden.
 */

#ifndef KONA_BRIDGE_H
#define KONA_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Konfiguration für die Kona-Bridge */
typedef struct {
  uint32_t    chain_id;               /* Chain ID (z.B. 10 für OP, 8453 für Base) */
  uint32_t    hardfork;               /* Hardfork Version (meist 4) */
  uint32_t    disc_port;              /* Discovery Port */
  uint32_t    gossip_port;            /* Gossip Port */
  uint32_t    ttl_minutes;            /* TTL für Preconfs in Minuten */
  uint32_t    cleanup_interval;       /* Cleanup-Intervall in Minuten */
  uint32_t    http_poll_interval;     /* HTTP-Polling Intervall in Sekunden (default: 2) */
  uint32_t    http_failure_threshold; /* Anzahl HTTP-Fehler vor Gossip-Umschaltung (default: 5) */
  const char* output_dir;             /* Output-Verzeichnis (kann NULL sein für Default) */
  const char* sequencer_address;      /* Expected Sequencer Address (kann NULL sein) */
  const char* chain_name;             /* Chain Name für Logging (kann NULL sein) */
} KonaBridgeConfig;

/* Opaque Handle für Bridge-Instanz */
typedef struct KonaBridgeHandle KonaBridgeHandle;

/* Statistiken der Bridge */
typedef struct {
  uint32_t connected_peers;    /* Anzahl verbundener Peers */
  uint32_t received_preconfs;  /* Anzahl empfangener Preconfs (gesamt) */
  uint32_t processed_preconfs; /* Anzahl verarbeiteter Preconfs (gesamt) */
  uint32_t failed_preconfs;    /* Anzahl fehlgeschlagener Preconfs (gesamt) */
  uint32_t http_received;      /* Anzahl über HTTP empfangener Preconfs */
  uint32_t http_processed;     /* Anzahl über HTTP verarbeiteter Preconfs */
  uint32_t gossip_received;    /* Anzahl über Gossip empfangener Preconfs */
  uint32_t gossip_processed;   /* Anzahl über Gossip verarbeiteter Preconfs */
  uint32_t mode_switches;      /* Anzahl HTTP→Gossip Umschaltungen */
  uint32_t current_mode;       /* Aktueller Modus: 0=HTTP, 1=Gossip */
} KonaBridgeStats;

/**
 * Initialisiert das Logging-System der Bridge
 * Sollte einmal beim Programmstart aufgerufen werden
 */
void kona_bridge_init_logging(void);

/**
 * Startet die Kona-Bridge mit der gegebenen Konfiguration
 *
 * @param config Zeiger auf KonaBridgeConfig
 * @return Handle zur Bridge-Instanz oder NULL bei Fehler
 */
KonaBridgeHandle* kona_bridge_start(const KonaBridgeConfig* config);

/**
 * Stoppt die Kona-Bridge und gibt Ressourcen frei
 *
 * @param handle Handle zur Bridge-Instanz
 */
void kona_bridge_stop(KonaBridgeHandle* handle);

/**
 * Prüft ob die Bridge läuft
 *
 * @param handle Handle zur Bridge-Instanz
 * @return 1 wenn laufend, 0 wenn gestoppt oder Fehler
 */
int kona_bridge_is_running(const KonaBridgeHandle* handle);

/**
 * Gibt aktuelle Statistiken der Bridge zurück
 *
 * @param handle Handle zur Bridge-Instanz
 * @param stats Zeiger auf KonaBridgeStats-Struktur
 * @return 0 bei Erfolg, -1 bei Fehler
 */
int kona_bridge_get_stats(const KonaBridgeHandle* handle, KonaBridgeStats* stats);

/* Integration-Funktionen (benötigen op_chains_conf.h) */
#ifdef OP_CHAINS_CONF_H

/**
 * Startet die Kona-Bridge basierend auf zentraler Chain-Konfiguration
 * Empfohlene Methode für einheitliche Konfiguration
 *
 * @param chain_config Zeiger auf op_chain_config_t aus zentraler Konfiguration
 * @param output_dir Output-Verzeichnis für Preconfs
 * @return 0 bei Erfolg, -1 bei Fehler
 */
int start_kona_bridge_from_config(const op_chain_config_t* chain_config, const char* output_dir);

/**
 * Legacy-Wrapper für Rückwärtskompatibilität
 */
int start_kona_bridge(uint32_t chain_id, const char* output_dir,
                      const char* sequencer_address, const char* chain_name);

/**
 * Stoppt die aktive Kona-Bridge
 */
void stop_kona_bridge(void);

/**
 * Prüft ob die Bridge läuft
 */
int is_kona_bridge_running(void);

/**
 * Gibt Statistiken der aktiven Bridge zurück
 */
int get_kona_bridge_stats(KonaBridgeStats* stats);

#endif /* OP_CHAINS_CONF_H */

/* Hilfsmakros für einfache Konfiguration */
#define KONA_BRIDGE_CONFIG_DEFAULT() { \
    .chain_id               = 10,      \
    .hardfork               = 4,       \
    .disc_port              = 9090,    \
    .gossip_port            = 9091,    \
    .ttl_minutes            = 30,      \
    .cleanup_interval       = 5,       \
    .http_poll_interval     = 2,       \
    .http_failure_threshold = 5,       \
    .output_dir             = NULL,    \
    .sequencer_address      = NULL,    \
    .chain_name             = NULL}

#define KONA_BRIDGE_CONFIG_BASE() {                                         \
    .chain_id               = 8453,                                         \
    .hardfork               = 4,                                            \
    .disc_port              = 9090,                                         \
    .gossip_port            = 9091,                                         \
    .ttl_minutes            = 30,                                           \
    .cleanup_interval       = 5,                                            \
    .http_poll_interval     = 2,                                            \
    .http_failure_threshold = 5,                                            \
    .output_dir             = NULL,                                         \
    .sequencer_address      = "0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a", \
    .chain_name             = "Base"}

#ifdef __cplusplus
}
#endif

#endif /* KONA_BRIDGE_H */
