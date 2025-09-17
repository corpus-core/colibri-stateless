// types.rs - Datenstrukturen und Enums für die Kona-Bridge

use std::{
    collections::HashSet,
    os::raw::{c_char, c_uint},
    sync::{Arc, Mutex},
    thread,
    time::SystemTime,
};
use tokio::runtime::Runtime;

/// Bridge-Modus für HTTP-first mit Gossip-Backup
#[derive(Debug, Clone, PartialEq)]
pub enum BridgeMode {
    HttpOnly,       // Nur HTTP-Polling aktiv
    HttpPlusGossip, // HTTP + Gossip parallel (bei Problemen)
    GossipFallback, // Nur Gossip aktiv (bei kompletter HTTP-Ausfall)
}

/// Tracker für HTTP-Gesundheit und Umschaltlogik
#[derive(Debug)]
pub struct HttpHealthTracker {
    pub consecutive_failures: u32,
    pub last_success: Option<SystemTime>,
    pub failure_threshold: u32,
    pub current_mode: BridgeMode,
    // Gap-basierte Umschaltung
    pub total_gaps: u32,  // Gesamtanzahl verpasster Blöcke
    pub recent_gaps: u32, // Gaps in letzten 10 Minuten
    pub last_gap_reset: Option<SystemTime>,
    pub gap_threshold: u32, // Max gaps before switching (default: 10)
    // Hybrid-Modus Tracking
    pub consecutive_success_blocks: u32, // Lückenlose Blöcke in Folge
    pub success_threshold: u32,          // Blöcke ohne Gap für Gossip-Stopp (default: 50)
}

/// C-kompatible Konfiguration für die Kona-Bridge
#[repr(C)]
pub struct KonaBridgeConfig {
    pub chain_id: c_uint,
    pub hardfork: c_uint,
    pub disc_port: c_uint,
    pub gossip_port: c_uint,
    pub ttl_minutes: c_uint,
    pub cleanup_interval: c_uint,
    pub http_poll_interval: c_uint, // HTTP-Polling Intervall in Sekunden (default: 2)
    pub http_failure_threshold: c_uint, // Anzahl aufeinanderfolgender HTTP-Fehler vor Gossip-Umschaltung (default: 5)
    pub output_dir: *const c_char,
    pub sequencer_address: *const c_char, // kann NULL sein
    pub chain_name: *const c_char,        // kann NULL sein
}

/// Deduplizierung für Race-Condition-Schutz
pub struct BlockDeduplicator {
    pub processed_blocks: HashSet<u64>, // Set der bereits verarbeiteten Block-Nummern
    pub max_size: usize,                // Maximale Anzahl gespeicherter Block-Nummern
}

/// Bitmask-basierter Gap-Tracker für präzise Block-Verfolgung
pub struct BlockBitmaskTracker {
    pub current_offset: u64,   // Erste Block-Nummer der aktuellen Bitmask
    pub current_mask: u64,     // Bitmask für Blöcke current_offset..current_offset+63
    pub total_gaps_found: u32, // Gesamtanzahl gefundener Gaps
}

impl BlockDeduplicator {
    pub fn new() -> Self {
        Self {
            processed_blocks: HashSet::with_capacity(200), // Pre-allocate for 200 blocks
            max_size: 200, // Reduziert von 1000 auf 200 für Memory-Effizienz
        }
    }
    pub fn is_duplicate(&mut self, block_number: u64) -> bool {
        if self.processed_blocks.contains(&block_number) {
            return true;
        }

        // Füge Block hinzu
        self.processed_blocks.insert(block_number);

        // Cleanup alter Blöcke wenn zu viele
        if self.processed_blocks.len() > self.max_size {
            // Entferne die kleinsten (ältesten) Block-Nummern
            let mut blocks: Vec<u64> = self.processed_blocks.iter().cloned().collect();
            blocks.sort();
            let keep_from = blocks.len() - (self.max_size / 2); // Behalte neuere Hälfte
            self.processed_blocks = blocks[keep_from..].iter().cloned().collect();
        }

        false
    }
}

impl BlockBitmaskTracker {
    pub fn new() -> Self {
        Self {
            current_offset: 0,
            current_mask: 0,
            total_gaps_found: 0,
        }
    }

    /// Markiert einen Block als verarbeitet und prüft auf Gaps
    pub fn mark_block_processed(&mut self, block_number: u64) {
        // Initialisierung beim ersten Block
        if self.current_offset == 0 {
            self.current_offset = block_number;
            self.current_mask = 1; // Erstes Bit setzen
            tracing::debug!("🎯 BITMASK: Initialized with block {}", block_number);
            return;
        }

        // Prüfe ob Block in aktuelle Bitmask passt
        if block_number >= self.current_offset && block_number < self.current_offset + 64 {
            // Block passt in aktuelle Bitmask
            let bit_position = block_number - self.current_offset;
            self.current_mask |= 1u64 << bit_position;
            tracing::debug!(
                "🎯 BITMASK: Set bit {} for block {}",
                bit_position,
                block_number
            );
        } else if block_number >= self.current_offset + 64 {
            // Block ist zu weit vorne - wir müssen die Bitmask verschieben
            self.finalize_current_bitmask();

            // Neue Bitmask starten
            self.current_offset = block_number;
            self.current_mask = 1; // Erstes Bit für neuen Block
            tracing::debug!("🎯 BITMASK: New bitmask starting at block {}", block_number);
        }
        // Blocks die zu alt sind (< current_offset) ignorieren wir
    }

    /// Finalisiert die aktuelle Bitmask und zählt Gaps
    fn finalize_current_bitmask(&mut self) {
        // Zähle fehlende Bits in der aktuellen Bitmask
        let gaps_in_mask = self.count_gaps_in_mask();
        self.total_gaps_found += gaps_in_mask;

        if gaps_in_mask > 0 {
            tracing::info!(
                "🎯 BITMASK: Found {} gaps in range {}-{}",
                gaps_in_mask,
                self.current_offset,
                self.current_offset + 63
            );
        }
    }

    /// Zählt Gaps in der aktuellen Bitmask
    fn count_gaps_in_mask(&self) -> u32 {
        if self.current_mask == 0 {
            return 0;
        }

        // Finde das höchste gesetzte Bit (letzter verarbeiteter Block)
        let highest_bit = 63 - self.current_mask.leading_zeros();

        // Zähle fehlende Bits von 0 bis highest_bit
        let mut gaps = 0;
        for i in 0..=highest_bit {
            if (self.current_mask & (1u64 << i)) == 0 {
                gaps += 1;
            }
        }

        gaps
    }

    /// Gibt aktuelle Gap-Statistiken zurück
    pub fn get_total_gaps(&mut self) -> u32 {
        // Finalisiere aktuelle Bitmask für aktuelle Zählung
        if self.current_mask != 0 {
            let current_gaps = self.count_gaps_in_mask();
            self.total_gaps_found + current_gaps
        } else {
            self.total_gaps_found
        }
    }
}

/// Handle für die laufende Bridge-Instanz
#[allow(dead_code)]
pub struct KonaBridgeHandle {
    pub config: KonaBridgeConfig,
    pub runtime: Option<Runtime>,
    pub stats: Arc<Mutex<KonaBridgeStats>>,
    pub running: Arc<Mutex<bool>>,
    pub thread_handle: Option<thread::JoinHandle<()>>,
    pub deduplicator: Arc<Mutex<BlockDeduplicator>>, // Race-Condition-Schutz
    pub bitmask_tracker: Arc<Mutex<BlockBitmaskTracker>>, // Präziser Gap-Tracker
}

/// Statistiken der Bridge
#[repr(C)]
pub struct KonaBridgeStats {
    pub connected_peers: c_uint,
    pub received_preconfs: c_uint,
    pub processed_preconfs: c_uint,
    pub failed_preconfs: c_uint,
    pub http_received: c_uint,
    pub http_processed: c_uint,
    pub gossip_received: c_uint,
    pub gossip_processed: c_uint,
    pub mode_switches: c_uint,
    pub current_mode: c_uint, // 0=HTTP, 1=Gossip
    pub total_gaps: c_uint,   // Gesamtanzahl verpasster Blöcke
    pub http_gaps: c_uint,    // Verpasste Blöcke während HTTP-Modus
    pub gossip_gaps: c_uint,  // Verpasste Blöcke während Gossip-Modus
}
