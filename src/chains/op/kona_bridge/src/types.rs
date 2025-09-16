// types.rs - Datenstrukturen und Enums für die Kona-Bridge

use std::{
    os::raw::{c_char, c_uint},
    sync::{Arc, Mutex},
    thread,
    time::SystemTime,
};
use tokio::runtime::Runtime;

/// Bridge-Modus für HTTP-first mit Gossip-Fallback
#[derive(Debug, Clone, PartialEq)]
pub enum BridgeMode {
    HttpPrimary,    // HTTP-Polling aktiv
    GossipFallback, // Gossip aktiv nach HTTP-Fehlern
}

/// Tracker für HTTP-Gesundheit und Umschaltlogik
#[derive(Debug)]
pub struct HttpHealthTracker {
    pub consecutive_failures: u32,
    pub last_success: Option<SystemTime>,
    pub failure_threshold: u32,
    pub current_mode: BridgeMode,
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

/// Handle für die laufende Bridge-Instanz
#[allow(dead_code)]
pub struct KonaBridgeHandle {
    pub config: KonaBridgeConfig,
    pub runtime: Option<Runtime>,
    pub stats: Arc<Mutex<KonaBridgeStats>>,
    pub running: Arc<Mutex<bool>>,
    pub thread_handle: Option<thread::JoinHandle<()>>,
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
}
