// kona_preconf_capture.h - Direkte Kona-Bridge Integration Header
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

// Include the actual KonaBridgeStats definition
#include "../kona_bridge/kona_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Startet die Kona-P2P Preconf-Erfassung in einem UV-Worker
 *
 * @param loop UV-Loop des Servers
 * @param chain_id Chain ID für die Konfiguration
 * @param output_dir Verzeichnis für Preconf-Dateien
 * @return 0 bei Erfolg, != 0 bei Fehler
 */
int start_kona_preconf_capture(uv_loop_t* loop, uint64_t chain_id, const char* output_dir);

/**
 * Stoppt die Kona-P2P Preconf-Erfassung
 *
 * @return 0 bei Erfolg, != 0 bei Fehler
 */
int stop_kona_preconf_capture(void);

/**
 * Prüft ob die Kona-Preconf-Erfassung läuft
 *
 * @return true wenn aktiv, false sonst
 */
bool is_kona_preconf_capture_running(void);

/**
 * Gibt Statistiken der Kona-Bridge zurück
 *
 * @param stats Zeiger auf KonaBridgeStats-Struktur
 * @return 0 bei Erfolg, -1 bei Fehler
 */
int get_kona_preconf_capture_stats(KonaBridgeStats* stats);

#ifdef __cplusplus
}
#endif
