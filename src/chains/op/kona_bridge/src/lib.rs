// lib.rs - Kona-Bridge: Bibliothek (C-FFI via feature "ffi") und Basis für Standalone-Binary

pub mod config;
pub mod gossip;
pub mod http;
pub mod processing;
pub mod types;
pub mod utils;
pub mod server;

use config::ChainConfig;
use http::run_http_primary_with_gossip_fallback;
use types::{BridgeMode, BlockBitmaskTracker, BlockDeduplicator, HttpHealthTracker, KonaBridgeStats};
use utils::cleanup_old_files;

use alloy::primitives::Address;
use std::{
    path::PathBuf,
    sync::{Arc, Mutex},
};
use tokio::sync::broadcast;
use tracing::{info, warn};
#[cfg(feature = "ffi")]
use tracing::error;

#[cfg(feature = "ffi")]
use std::{ffi::CStr, fs, os::raw::c_int, thread, time::Duration};
#[cfg(feature = "ffi")]
use types::{KonaBridgeConfig, KonaBridgeHandle};


/// Startet die echte Kona-Bridge mit korrektem Preconf-Format
#[cfg(feature = "ffi")]
#[no_mangle]
pub extern "C" fn kona_bridge_start(config: *const KonaBridgeConfig) -> *mut KonaBridgeHandle {
    eprintln!("🚀 kona_bridge_start called from C");
    
    if config.is_null() {
        eprintln!("❌ Null config provided to kona_bridge_start");
        return std::ptr::null_mut();
    }

    eprintln!("✅ Config is not null, proceeding...");
    let config = unsafe { &*config };
    eprintln!("✅ Config dereferenced successfully");

    // Convert C strings to Rust strings
    eprintln!("🔧 Converting output_dir...");
    let output_dir = unsafe {
        if config.output_dir.is_null() {
            eprintln!("🔧 output_dir is NULL, using default");
            "./preconfs".to_string()
        } else {
            eprintln!("🔧 output_dir is not NULL, converting...");
            match CStr::from_ptr(config.output_dir).to_str() {
                Ok(s) => {
                    eprintln!("🔧 output_dir converted successfully: {}", s);
                    s.to_string()
                },
                Err(e) => {
                    eprintln!("❌ Invalid UTF-8 in output_dir: {:?}", e);
                    return std::ptr::null_mut();
                }
            }
        }
    };

    eprintln!("🔧 Converting sequencer_address...");
    let sequencer_address = unsafe {
        if config.sequencer_address.is_null() {
            eprintln!("🔧 sequencer_address is NULL");
            None
        } else {
            eprintln!("🔧 sequencer_address is not NULL, converting from binary bytes...");
            // The sequencer_address is stored as 20 binary bytes, not a UTF-8 string
            // We need to convert it to a hex string
            let bytes = std::slice::from_raw_parts(config.sequencer_address as *const u8, 20);
            let hex_string = format!("0x{}", hex::encode(bytes));
            eprintln!("🔧 sequencer_address converted successfully: {}", hex_string);
            Some(hex_string)
        }
    };

    eprintln!("🔧 Converting chain_name...");
    let _chain_name = unsafe {
        if config.chain_name.is_null() {
            eprintln!("🔧 chain_name is NULL");
            None
        } else {
            eprintln!("🔧 chain_name is not NULL, converting...");
            match CStr::from_ptr(config.chain_name).to_str() {
                Ok(s) => {
                    eprintln!("🔧 chain_name converted successfully: {}", s);
                    Some(s.to_string())
                },
                Err(e) => {
                    eprintln!("❌ Invalid UTF-8 in chain_name: {:?}", e);
                    return std::ptr::null_mut();
                }
            }
        }
    };

    eprintln!("✅ String conversions completed successfully");
    eprintln!("📁 Output dir: {}", output_dir);
    info!("🚀 Starting Kona-P2P Bridge from C interface");
    info!("⛓️  Chain ID: {}", config.chain_id);
    info!("📁 Output: {}", output_dir);

    // Create output directory
    if let Err(e) = fs::create_dir_all(&output_dir) {
        error!("❌ Failed to create output directory: {}", e);
        return std::ptr::null_mut();
    }

    let stats = Arc::new(Mutex::new(KonaBridgeStats {
        connected_peers: 0,
        received_preconfs: 0,
        processed_preconfs: 0,
        failed_preconfs: 0,
        http_received: 0,
        http_processed: 0,
        gossip_received: 0,
        gossip_processed: 0,
        mode_switches: 0,
        current_mode: 0, // Start im HTTP-Modus
        total_gaps: 0,   // Gesamtanzahl verpasster Blöcke
        http_gaps: 0,    // Verpasste Blöcke während HTTP-Modus
        gossip_gaps: 0,  // Verpasste Blöcke während Gossip-Modus
        bitmask_gaps: 0, // Präzise Gaps via Bitmask-Tracking
    }));

    let running = Arc::new(Mutex::new(true));

    // Start Kona network in separate thread
    let stats_clone = stats.clone();
    let running_clone = running.clone();
    let chain_id = config.chain_id as u64;
    let disc_port = config.disc_port as u16;
    let gossip_port = config.gossip_port as u16;
    let ttl_minutes = config.ttl_minutes as u64;
    let cleanup_interval = config.cleanup_interval as u64;
    let output_path = PathBuf::from(output_dir);
    
    info!("🧹 TTL cleanup configured: {}min TTL, {}min interval", ttl_minutes, cleanup_interval);

    // Extract values from config to avoid thread safety issues
    let http_poll_interval = config.http_poll_interval as u64;
    let http_failure_threshold = config.http_failure_threshold as u32;
    
    let thread_handle = thread::spawn(move || {
        info!("🔄 Kona-P2P bridge thread starting...");

        let rt = match tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)  // GPT-5 Hot-Thread Fix: Minimal worker count
            .max_blocking_threads(1)  // GPT-5: Nur 1 blocking thread für ZSTD
            .thread_name("kona-bridge")
            .thread_keep_alive(Duration::from_secs(10))  // Threads früher beenden
            .enable_all()
            .build() {
            Ok(rt) => rt,
            Err(e) => {
                error!("❌ Failed to create optimized Tokio runtime: {}", e);
                return;
            }
        };

        rt.block_on(async {
            info!("🔄 About to start HTTP-first Kona network for chain {}", chain_id);
            if let Err(e) = run_http_first_network(
                chain_id,
                disc_port,
                gossip_port,
                &output_path,
                ttl_minutes,
                cleanup_interval,
                http_poll_interval,
                http_failure_threshold,
                sequencer_address.as_deref(),
                stats_clone,
                running_clone,
                None, // No SSE in C-FFI mode
            ).await {
                error!("❌ HTTP-first network failed: {}", e);
                error!("❌ Error details: {:?}", e);
            } else {
                info!("✅ HTTP-first network completed successfully");
            }
        });

        info!("🛑 Kona-P2P bridge thread stopping...");
    });

    // Copy config for storage in handle
    let config_copy = KonaBridgeConfig {
        chain_id: config.chain_id,
        hardfork: config.hardfork,
        disc_port: config.disc_port,
        gossip_port: config.gossip_port,
        ttl_minutes: config.ttl_minutes,
        cleanup_interval: config.cleanup_interval,
        http_poll_interval: config.http_poll_interval,
        http_failure_threshold: config.http_failure_threshold,
        output_dir: config.output_dir,
        sequencer_address: config.sequencer_address,
        chain_name: config.chain_name,
    };

    let handle = KonaBridgeHandle {
        config: config_copy,
        runtime: None,
        stats,
        running,
        thread_handle: Some(thread_handle),
        deduplicator: Arc::new(Mutex::new(BlockDeduplicator::new())),
        bitmask_tracker: Arc::new(Mutex::new(BlockBitmaskTracker::new())),
    };

    info!("✅ Kona-P2P Bridge started successfully");
    Box::into_raw(Box::new(handle))
}

/// HTTP-first Netzwerk mit Gossip-Fallback (auch vom Standalone-Binary nutzbar)
pub async fn run_http_first_network(
    chain_id: u64,
    disc_port: u16,
    gossip_port: u16,
    output_dir: &PathBuf,
    ttl_minutes: u64,
    cleanup_interval: u64,
    http_poll_interval: u64,
    http_failure_threshold: u32,
    expected_sequencer: Option<&str>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
    sse_tx: Option<broadcast::Sender<u64>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    
    info!("🚀 HTTP-first network starting for chain {}", chain_id);
    info!("🧹 TTL cleanup: {} minutes, interval: {} minutes", ttl_minutes, cleanup_interval);
    info!("🌐 HTTP polling: {}s interval, {} failure threshold", http_poll_interval, http_failure_threshold);
    
    // Start TTL cleanup task
    let cleanup_output_dir = output_dir.clone();
    let cleanup_running = running.clone();
    tokio::spawn(async move {
        cleanup_old_files(cleanup_output_dir, ttl_minutes, cleanup_interval, cleanup_running).await;
    });
    
    // Determine network name from chain_id
    let network_name = match chain_id {
        10 => "op-mainnet",
        8453 => "base", 
        130 => "unichain",
        480 => "worldchain",
        7777777 => "zora",
        _ => {
            warn!("⚠️  Unknown chain ID {}, using default config", chain_id);
            "base" // fallback
        }
    };

    let mut chain_config = ChainConfig::from(network_name, chain_id);
    
    // Override with expected sequencer from C config if provided
    if let Some(expected) = expected_sequencer {
        if let Ok(addr) = expected.parse::<Address>() {
            chain_config.unsafe_signer = addr;
            info!("🔐 Using sequencer from C config: {}", addr);
        }
    }
    
    // Initialize HTTP health tracker with simplified switching
    let health_tracker = Arc::new(Mutex::new(HttpHealthTracker {
        consecutive_failures: 0,
        last_success: None,
        failure_threshold: http_failure_threshold,
        current_mode: BridgeMode::HttpOnly,
        consecutive_success_blocks: 0,
    }));
    
    // Try HTTP-first approach
    if let Some(http_endpoint) = chain_config.get_http_endpoint() {
        info!("🌐 Starting in HTTP-primary mode: {}", http_endpoint);
        
        // Start HTTP primary with fallback to gossip
        run_http_primary_with_gossip_fallback(
            http_endpoint,
            chain_id,
            disc_port,
            gossip_port,
            output_dir,
            http_poll_interval,
            &chain_config,
            expected_sequencer,
            health_tracker,
            stats,
            running,
            Arc::new(Mutex::new(BlockDeduplicator::new())),
            Arc::new(Mutex::new(BlockBitmaskTracker::new())),
            sse_tx,
        ).await?;
    } else {
        info!("🌐 No HTTP endpoint - starting directly in gossip mode");
        gossip::run_gossip_network(
            chain_id,
            disc_port,
            gossip_port,
            output_dir,
            &chain_config,
            expected_sequencer,
            stats,
            running,
            None,
            None,
            sse_tx,
        ).await?;
    }
    
    Ok(())
}

/// Stoppt die Kona-Bridge
#[cfg(feature = "ffi")]
#[no_mangle]
pub extern "C" fn kona_bridge_stop(handle: *mut KonaBridgeHandle) {
    if handle.is_null() {
        error!("❌ Null handle provided to kona_bridge_stop");
        return;
    }

    info!("🛑 Stopping Kona-P2P Bridge");
    
    let mut handle = unsafe { Box::from_raw(handle) };
    
    // Signal thread to stop
    {
        let mut running = handle.running.lock().unwrap();
        *running = false;
    }
    
    // Wait for thread to finish
    if let Some(thread_handle) = handle.thread_handle.take() {
        if let Err(e) = thread_handle.join() {
            error!("❌ Failed to join bridge thread: {:?}", e);
        }
    }
    
    info!("✅ Kona-P2P Bridge stopped");
}

/// Prüft ob die Bridge läuft
#[cfg(feature = "ffi")]
#[no_mangle]
pub extern "C" fn kona_bridge_is_running(handle: *const KonaBridgeHandle) -> c_int {
    if handle.is_null() {
        return 0;
    }
    
    let handle = unsafe { &*handle };
    if let Ok(running) = handle.running.lock() {
        if *running { 1 } else { 0 }
    } else {
        0
    }
}

/// Gibt Statistiken über die Bridge zurück
#[cfg(feature = "ffi")]
#[no_mangle]
pub extern "C" fn kona_bridge_get_stats(
    handle: *const KonaBridgeHandle,
    stats: *mut KonaBridgeStats,
) -> c_int {
    if handle.is_null() || stats.is_null() {
        return -1;
    }

    let handle = unsafe { &*handle };
    
    if let Ok(bridge_stats) = handle.stats.lock() {
        // Update bitmask gaps from tracker
        let bitmask_gaps = if let Ok(mut tracker) = handle.bitmask_tracker.lock() {
            tracker.get_total_gaps()
        } else {
            0
        };
        
        unsafe {
            (*stats).connected_peers = bridge_stats.connected_peers;
            (*stats).received_preconfs = bridge_stats.received_preconfs;
            (*stats).processed_preconfs = bridge_stats.processed_preconfs;
            (*stats).failed_preconfs = bridge_stats.failed_preconfs;
            (*stats).http_received = bridge_stats.http_received;
            (*stats).http_processed = bridge_stats.http_processed;
            (*stats).gossip_received = bridge_stats.gossip_received;
            (*stats).gossip_processed = bridge_stats.gossip_processed;
            (*stats).mode_switches = bridge_stats.mode_switches;
            (*stats).current_mode = bridge_stats.current_mode;
            (*stats).total_gaps = bridge_stats.total_gaps;
            (*stats).http_gaps = bridge_stats.http_gaps;
            (*stats).gossip_gaps = bridge_stats.gossip_gaps;
            (*stats).bitmask_gaps = bitmask_gaps;
        }
        0
    } else {
        -1
    }
}

/// Hilfsfunktion für Logging-Setup von C aus (CPU-optimized)
#[cfg(feature = "ffi")]
#[no_mangle]
pub extern "C" fn kona_bridge_init_logging() {
    use tracing_subscriber::{EnvFilter, fmt::format::FmtSpan};
    use std::sync::Once;
    
    static INIT: Once = Once::new();
    
    INIT.call_once(|| {
        eprintln!("🦀 [RUST] Initializing Rust tracing subscriber...");
        
        let filter = EnvFilter::try_from_default_env()
            .unwrap_or_else(|_| EnvFilter::new(
                "warn,libp2p=off,discv5=off,kona_p2p=off,kona_bridge=info,tokio=warn,hyper=warn,reqwest=warn"
            ));
            
        let result = tracing_subscriber::fmt()
            .with_env_filter(filter)
            .with_target(false)  // Reduziert String-Allocation
            .with_span_events(FmtSpan::NONE)  // Weniger Spam
            .with_line_number(false)  // Weniger Overhead
            .compact()  // Kompaktere Logs
            .with_writer(std::io::stderr)  // Explizit stderr verwenden
            .try_init();
            
        match result {
            Ok(_) => eprintln!("🦀 [RUST] Tracing subscriber initialized successfully"),
            Err(e) => eprintln!("🦀 [RUST] Tracing subscriber already initialized: {}", e),
        }
    });
}