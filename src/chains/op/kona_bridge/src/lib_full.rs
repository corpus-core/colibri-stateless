// lib.rs - Temporarily use minimal version due to dependency issues
// TODO: Switch back to full implementation once ethereum_ssz dependency is resolved

// Re-export minimal implementation
pub use crate::lib_minimal::*;

mod lib_minimal;

// Full implementation (currently disabled due to ethereum_ssz dependency issues)
// lib_helios.rs - C-kompatible Interface für echte Kona-Bridge mit korrektem Preconf-Format

use alloy::primitives::{Address, address};
use discv5::{ConfigBuilder, enr::CombinedKey};
use hex;
use kona_p2p::{LocalNode, Network};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{Multiaddr, identity::Keypair};
use op_alloy_rpc_types_engine::{OpNetworkPayloadEnvelope, OpExecutionPayload};
use ethereum_ssz::Encode;
use reqwest;
use std::{
    borrow::BorrowMut,
    ffi::CStr,
    fs,
    net::{IpAddr, Ipv4Addr, SocketAddr},
    os::raw::{c_char, c_int, c_uint},
    path::PathBuf,
    sync::{Arc, Mutex},
    thread,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{
    fs as tokio_fs,
    runtime::Runtime,
    time::{interval, sleep},
};
use tracing::{debug, error, info, warn};

/// Bridge-Modus für HTTP-first mit Gossip-Fallback
#[derive(Debug, Clone, PartialEq)]
enum BridgeMode {
    HttpPrimary,     // HTTP-Polling aktiv
    GossipFallback,  // Gossip aktiv nach HTTP-Fehlern
}

/// Tracker für HTTP-Gesundheit und Umschaltlogik
#[derive(Debug)]
struct HttpHealthTracker {
    consecutive_failures: u32,
    last_success: Option<SystemTime>,
    failure_threshold: u32,
    current_mode: BridgeMode,
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
    config: KonaBridgeConfig,
    runtime: Option<Runtime>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
    thread_handle: Option<thread::JoinHandle<()>>,
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

/// Startet die echte Kona-Bridge mit korrektem Preconf-Format
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
    };

    info!("✅ Kona-P2P Bridge started successfully");
    Box::into_raw(Box::new(handle))
}

/// HTTP-first Netzwerk mit Gossip-Fallback
async fn run_http_first_network(
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
    
    // Initialize HTTP health tracker
    let health_tracker = Arc::new(Mutex::new(HttpHealthTracker {
        consecutive_failures: 0,
        last_success: None,
        failure_threshold: http_failure_threshold,
        current_mode: BridgeMode::HttpPrimary,
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
        ).await?;
    } else {
        info!("🌐 No HTTP endpoint - starting directly in gossip mode");
        // No HTTP endpoint available, start gossip directly
        run_gossip_network(
            chain_id,
            disc_port,
            gossip_port,
            output_dir,
            &chain_config,
            expected_sequencer,
            stats,
            running,
        ).await?;
    }
    
    Ok(())
}

/// HTTP-Primary mit Gossip-Fallback
async fn run_http_primary_with_gossip_fallback(
    http_endpoint: String,
    chain_id: u64,
    disc_port: u16,
    gossip_port: u16,
    output_dir: &PathBuf,
    http_poll_interval: u64,
    chain_config: &ChainConfig,
    expected_sequencer: Option<&str>,
    health_tracker: Arc<Mutex<HttpHealthTracker>>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    
    let client = reqwest::Client::new();
    let mut interval_timer = interval(Duration::from_secs(http_poll_interval));
    let mut last_data_hash: Option<String> = None;
    
    info!("🌐 HTTP-primary mode active: {} ({}s interval)", http_endpoint, http_poll_interval);
    
    while *running.lock().unwrap() {
        interval_timer.tick().await;
        
        if !*running.lock().unwrap() {
            break;
        }
        
        // Check if we should switch to gossip mode
        {
            let tracker = health_tracker.lock().unwrap();
            if tracker.current_mode == BridgeMode::GossipFallback {
                info!("🌐➡️📡 SWITCHING: HTTP → P2P Gossip (failure threshold reached)");
                drop(tracker);
                break;
            }
        }
        
        // Try HTTP polling
        match fetch_http_preconf(&client, &http_endpoint).await {
            Ok(Some((data_hash, preconf_data))) => {
                // Check if this is new data
                if let Some(ref last_hash) = last_data_hash {
                    if *last_hash == data_hash {
                        continue; // Same data, skip
                    }
                }
                
                last_data_hash = Some(data_hash.clone());
                info!("🌐 HTTP: New preconf received (hash: {})", &data_hash[..16]);
                
                // Reset failure counter on success
                {
                    let mut tracker = health_tracker.lock().unwrap();
                    tracker.consecutive_failures = 0;
                    tracker.last_success = Some(SystemTime::now());
                }
                
                // Process and save the preconf
                match process_http_preconf(preconf_data, chain_id, output_dir).await {
                    Ok(block_number) => {
                        // Update stats
                        {
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.received_preconfs += 1;
                            stats_guard.processed_preconfs += 1;
                            stats_guard.http_received += 1;
                            stats_guard.http_processed += 1;
                        }
                        info!("📡 HTTP: Processed preconf for block {} (source: HTTP polling)", block_number);
                    }
                    Err(e) => {
                        warn!("🌐 HTTP: Failed to process preconf: {}", e);
                        {
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.received_preconfs += 1;
                            stats_guard.failed_preconfs += 1;
                            stats_guard.http_received += 1;
                        }
                    }
                }
            }
            Ok(None) => {
                // No new data, but HTTP is working
                {
                    let mut tracker = health_tracker.lock().unwrap();
                    tracker.consecutive_failures = 0;
                    tracker.last_success = Some(SystemTime::now());
                }
                continue;
            }
            Err(e) => {
                warn!("🌐 HTTP: Polling failed: {}", e);
                
                // Increment failure counter
                let should_switch = {
                    let mut tracker = health_tracker.lock().unwrap();
                    tracker.consecutive_failures += 1;
                    warn!("🌐 HTTP: Consecutive failures: {}/{}", tracker.consecutive_failures, tracker.failure_threshold);
                    
                    if tracker.consecutive_failures >= tracker.failure_threshold {
                        tracker.current_mode = BridgeMode::GossipFallback;
                        true
                    } else {
                        false
                    }
                };
                
                if should_switch {
                    // Update mode switch statistics
                    {
                        let mut stats_guard = stats.lock().unwrap();
                        stats_guard.mode_switches += 1;
                        stats_guard.current_mode = 1; // Gossip mode
                    }
                    warn!("🌐➡️📡 HTTP failure threshold reached - switching to Gossip mode");
                    break;
                } else {
                    // Wait longer on error
                    sleep(Duration::from_secs(30)).await;
                }
            }
        }
    }
    
    // If we reach here, either we're stopping or switching to gossip
    if *running.lock().unwrap() {
        let tracker = health_tracker.lock().unwrap();
        if tracker.current_mode == BridgeMode::GossipFallback {
            drop(tracker);
            info!("🚀 Starting Gossip network as fallback...");
            return run_gossip_network(
                chain_id,
                disc_port,
                gossip_port,
                output_dir,
                chain_config,
                expected_sequencer,
                stats,
                running,
            ).await;
        }
    }
    
    info!("🌐 HTTP-primary mode stopped");
    Ok(())
}

/// Reine Gossip-Netzwerk Implementierung
async fn run_gossip_network(
    chain_id: u64,
    disc_port: u16,
    gossip_port: u16,
    output_dir: &PathBuf,
    chain_config: &ChainConfig,
    expected_sequencer: Option<&str>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    
    let gossip = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)), gossip_port);
    let mut gossip_addr = Multiaddr::from(gossip.ip());
    gossip_addr.push(libp2p::multiaddr::Protocol::Tcp(gossip.port()));

    let CombinedKey::Secp256k1(k256_key) = CombinedKey::generate_secp256k1() else {
        return Err("Failed to generate secp256k1 key".into());
    };
    
    let advertise_ip = IpAddr::V4(Ipv4Addr::UNSPECIFIED);
    let disc = LocalNode::new(k256_key, advertise_ip, disc_port, disc_port);
    let disc_listen = SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), disc_port);

    let gossip_key = Keypair::generate_secp256k1();

    info!("🔍 Looking up rollup config for chain {}", chain_config.chain_id);
    let cfg = ROLLUP_CONFIGS
        .get(&chain_config.chain_id)
        .or_else(|| {
            warn!("⚠️  Rollup config not found for chain {}, using Base as fallback", chain_config.chain_id);
            ROLLUP_CONFIGS.get(&8453) // Use Base as fallback
        })
        .ok_or_else(|| format!("No rollup config found for chain {} or Base fallback", chain_config.chain_id))?
        .clone();
    
    info!("✅ Found rollup config for chain {}", chain_config.chain_id);

    info!("🔍 Discovery: 0.0.0.0:{}", disc_port);
    info!("📡 Gossip: 0.0.0.0:{}", gossip_port);
    info!("🔐 Expected sequencer: {}", chain_config.unsafe_signer);

    // Thread-optimized P2P: Use first 3 bootnodes for reliable discovery
    let optimized_bootnodes = if chain_config.bootnodes.len() > 3 {
        chain_config.bootnodes[0..3].to_vec() // Use first 3 bootnodes
    } else {
        chain_config.bootnodes.clone() // Use all available
    };
    
    info!("🔧 Thread-optimized P2P: Using {} bootnode(s) for reliable discovery", optimized_bootnodes.len());
    
    let mut network = Network::builder()
        .with_rollup_config(cfg)
        .with_unsafe_block_signer(chain_config.unsafe_signer)
        .with_discovery_address(disc)
        .with_gossip_address(gossip_addr)
        .with_keypair(gossip_key)
        .with_discovery_config(
            ConfigBuilder::new(disc_listen.into())
                .build() // Minimal discovery config
        )
        .build()
        .map_err(|e| format!("Failed to build P2P network: {}", e))?;

    // Add optimized bootnodes for reliable peer discovery
    for bootnode in optimized_bootnodes {
        info!("🔗 Adding bootnode: {}", bootnode);
        network
            .discovery
            .borrow_mut()
            .disc
            .borrow_mut()
            .add_enr(bootnode)
            .map_err(|e| format!("Failed to add bootnode: {}", e))?;
    }

    let mut payload_recv = network.unsafe_block_recv();
    network
        .start()
        .await
        .map_err(|e| format!("Failed to start P2P network: {}", e))?;

    info!("✅ P2P network started successfully!");
    info!("🎧 Listening for gossip messages...");

    // Update stats
    {
        let mut stats_guard = stats.lock().unwrap();
        stats_guard.connected_peers = 1; // Will update with real peer count
    }

    let mut latest_block_number = 0u64;

    // Optimized event loop with balanced polling frequency
    while *running.lock().unwrap() {
        // CPU-optimized: Check every 5 seconds (balance between responsiveness and CPU)
        tokio::time::sleep(Duration::from_secs(5)).await;
        
        if !*running.lock().unwrap() {
            break;
        }
        
        // Check for new payloads (every 5 seconds)
        match payload_recv.try_recv() {
            Ok(payload_envelope) => {
                let hash = payload_envelope.payload.block_hash();
                let number = payload_envelope.payload.block_number();
                
                info!("🎉 P2P: PRECONF RECEIVED! Block #{} Hash: {}", number, hash);

                // Update received stats
                {
                    let mut stats_guard = stats.lock().unwrap();
                    stats_guard.received_preconfs += 1;
                    stats_guard.gossip_received += 1;
                }

                // Process preconf (only if newer)
                if number > latest_block_number {
                    match process_preconf_with_correct_format(
                        &payload_envelope, 
                        chain_id, 
                        output_dir, 
                        expected_sequencer
                    ).await {
                        Ok(()) => {
                            latest_block_number = number;
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.processed_preconfs += 1;
                            stats_guard.gossip_processed += 1;
                            info!("✅ GOSSIP: Processed (total: {})", stats_guard.processed_preconfs);
                        }
                        Err(e) => {
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.failed_preconfs += 1;
                            error!("❌ GOSSIP: Failed: {}", e);
                        }
                    }
                } else {
                    info!("⏭️  GOSSIP: Skipping old preconf #{}", number);
                }
            }
            Err(tokio::sync::broadcast::error::TryRecvError::Empty) => {
                // GPT-5 Hot-Thread Fix: yield_now() statt busy-waiting
                tokio::task::yield_now().await;
                continue;
            }
            Err(tokio::sync::broadcast::error::TryRecvError::Closed) => {
                info!("🛑 GOSSIP: Receiver closed");
                break;
            }
            Err(tokio::sync::broadcast::error::TryRecvError::Lagged(_)) => {
                warn!("⚠️  GOSSIP: Receiver lagged");
                continue;
            }
        }
    }

    info!("🛑 Gossip network stopping...");
    Ok(())
}

async fn process_preconf_with_correct_format(
    payload_envelope: &OpNetworkPayloadEnvelope,
    chain_id: u64,
    output_dir: &PathBuf,
    _expected_sequencer: Option<&str>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let block_number = payload_envelope.payload.block_number();
    let block_hash = payload_envelope.payload.block_hash();
    
    info!("📦 Processing preconf for block #{}", block_number);

    // Extract the execution payload bytes using SSZ serialization (EXACTLY like Helios)
    // This is the exact format used for signature verification
    let execution_payload_bytes = match &payload_envelope.payload {
        OpExecutionPayload::V1(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V2(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V3(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V4(payload) => payload.as_ssz_bytes(),
    };
    
    // Debug logs moved to debug! level (CPU-optimized)
    debug!("🔍 ExecutionPayload type: {:?}", std::any::type_name_of_val(&payload_envelope.payload));
    debug!("🔍 ExecutionPayload serialized size: {} bytes", execution_payload_bytes.len());
    
    // Format: parent_beacon_block_root + execution_payload (EXACTLY like Helios)
    let mut preconf_data = Vec::new();
    
    // Use parent_beacon_block_root (like Helios), not zero domain
    if let Some(parent_root) = payload_envelope.parent_beacon_block_root {
        preconf_data.extend_from_slice(parent_root.as_slice()); // 32-byte parent root
        debug!("🔍 Using parent_beacon_block_root: {:02x?}", &parent_root.as_slice()[..16]);
    } else {
        // Fallback to zero domain if no parent root (shouldn't happen normally)
        preconf_data.extend_from_slice(&[0u8; 32]); // 32-byte zero domain
        debug!("⚠️  No parent_beacon_block_root, using zero domain");
    }
    
    preconf_data.extend_from_slice(&execution_payload_bytes);
    
    info!("📊 Preconf data: {} bytes (domain: 32, payload: {})", 
          preconf_data.len(), execution_payload_bytes.len());

    // GPT-5 Hot-Thread Fix: ZSTD mit bulk compressor (noch effizienter)
    let compressed_payload = {
        let preconf_data_clone = preconf_data.clone();
        tokio::task::spawn_blocking(move || -> Result<Vec<u8>, Box<dyn std::error::Error + Send + Sync>> {
            // Bulk-Kompression ist effizienter als Stream für kleine Daten
            let compressed = zstd::bulk::compress(&preconf_data_clone, 1)?; // Level 1
            Ok(compressed)
        }).await??
    };

    // Extract signature (65 bytes) with correct v-parameter
    let signature_bytes = signature_to_bytes(&payload_envelope.signature);

    // Format: compressed_payload + signature (65 bytes) - same as Go implementation
    let mut final_data = Vec::new();
    final_data.extend_from_slice(&compressed_payload);
    final_data.extend_from_slice(&signature_bytes);

    // Write to file: block_{chain_id}_{block_number}.raw (using REAL block number)
    let filename = format!("block_{}_{}.raw", chain_id, block_number);
    let filepath = output_dir.join(&filename);
    
    // Atomic write
    let temp_filepath = filepath.with_extension("tmp");
    tokio_fs::write(&temp_filepath, &final_data).await
        .map_err(|e| format!("Failed to write temp file: {}", e))?;
    tokio_fs::rename(&temp_filepath, &filepath).await
        .map_err(|e| format!("Failed to rename temp file: {}", e))?;

    info!("💾 Saved preconf to: {:?} ({} bytes)", filepath, final_data.len());

    // Update symlinks (latest.raw and pre_latest.raw)
    update_symlinks_lib(output_dir, &filename, chain_id).await?;

    // Create metadata file (same structure as before)
    let meta_filename = format!("block_{}_{}.json", chain_id, block_number);
    let meta_filepath = output_dir.join(&meta_filename);
    
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
    let metadata = serde_json::json!({
        "chain_id": chain_id.to_string(),
        "block_number": block_number,
        "block_hash": format!("0x{:x}", block_hash),
        "received_unix": timestamp,
        "signature": format!("0x{}", hex::encode(&signature_bytes)),
        "compressed_size": compressed_payload.len(),
        "decompressed_size": preconf_data.len(),
        "file_path": filename,
        "source": "gossip",
        "kona_p2p": true
    });

    let metadata_json = serde_json::to_string_pretty(&metadata)
        .map_err(|e| format!("Failed to serialize metadata: {}", e))?;
    
    tokio_fs::write(&meta_filepath, metadata_json).await
        .map_err(|e| format!("Failed to write metadata: {}", e))?;

    info!("📡 GOSSIP: Processed preconf for block {} (source: Gossip network)", block_number);
    
    Ok(())
}

fn signature_to_bytes(signature: &alloy::signers::Signature) -> [u8; 65] {
    let mut bytes = [0u8; 65];
    bytes[..32].copy_from_slice(&signature.r().to_be_bytes::<32>());
    bytes[32..64].copy_from_slice(&signature.s().to_be_bytes::<32>());
    // Convert recovery ID (0/1) to Ethereum v parameter (27/28)
    bytes[64] = if signature.v() { 28 } else { 27 };
    bytes
}

/// Stoppt die Kona-Bridge
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
        }
        0
    } else {
        -1
    }
}

/// Hilfsfunktion für Logging-Setup von C aus (CPU-optimized)
#[no_mangle]
pub extern "C" fn kona_bridge_init_logging() {
    use tracing_subscriber::EnvFilter;
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env()
            .unwrap_or_else(|_| EnvFilter::new(
                // GPT-5 Hot-Thread Fix: Sehr konservative Defaults für weniger CPU-Last
                "warn,libp2p_swarm=warn,libp2p_tcp=warn,libp2p_gossipsub=warn,discv5=warn,kona_p2p=info,kona_bridge=info"
            )))
        .with_target(false)  // Reduziert String-Allocation
        .compact()          // Kompaktere Logs = weniger I/O
        .init();
}

struct ChainConfig {
    unsafe_signer: Address,
    chain_id: u64,
    bootnodes: Vec<discv5::Enr>,
}

impl ChainConfig {
    fn get_http_endpoint(&self) -> Option<String> {
        // Get HTTP endpoint from centralized chain configuration
        match self.chain_id {
            10 => Some("https://op-mainnet.operationsolarstorm.org/latest".to_string()),
            8453 => Some("https://base.operationsolarstorm.org/latest".to_string()),
            130 => Some("https://unichain.operationsolarstorm.org/latest".to_string()),
            480 => Some("https://worldchain.operationsolarstorm.org/latest".to_string()),
            7777777 => Some("https://zora.operationsolarstorm.org/latest".to_string()),
            _ => None,
        }
    }
    
    fn from(network: &str, chain_id: u64) -> Self {
        match network {
            "op-mainnet" => ChainConfig {
                unsafe_signer: address!("AAAA45d9549EDA09E70937013520214382Ffc4A2"),
                chain_id: 10,
                // Use the same working bootnodes as Unichain for now
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "base" => ChainConfig {
                unsafe_signer: address!("Af6E19BE0F9cE7f8afd49a1824851023A8249e8a"),
                chain_id: 8453,
                // Use the same working bootnodes as Unichain for now
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "unichain" => ChainConfig {
                unsafe_signer: address!("833C6f278474A78658af91aE8edC926FE33a230e"),
                chain_id: 130,
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "worldchain" => ChainConfig {
                unsafe_signer: address!("2270d6eC8E760daA317DD978cFB98C8f144B1f3A"),
                chain_id: 480,
                bootnodes: Vec::new(),
            },
            "zora" => ChainConfig {
                unsafe_signer: address!("3Dc8Dfd070C835cAd15a6A27e089FF4cF4C92280"),
                chain_id: 7777777,
                bootnodes: Vec::new(),
            },
            _ => {
                // Use provided chain_id for custom networks
                ChainConfig {
                    unsafe_signer: Address::ZERO, // Will be configured from C config
                    chain_id,
                    bootnodes: Vec::new(),
                }
            }
        }
    }
}

/// Cleanup-Funktion für TTL-basierte Löschung alter Preconf-Dateien
async fn cleanup_old_files(
    output_dir: PathBuf,
    ttl_minutes: u64,
    cleanup_interval_minutes: u64,
    running: Arc<Mutex<bool>>,
) {
    let ttl_duration = Duration::from_secs(ttl_minutes * 60);
    let cleanup_interval = Duration::from_secs(cleanup_interval_minutes * 60);
    
    info!("🧹 Starting TTL cleanup task: TTL={}min, interval={}min", ttl_minutes, cleanup_interval_minutes);
    
    let mut interval_timer = interval(cleanup_interval);
    
    while *running.lock().unwrap() {
        interval_timer.tick().await;
        
        if !*running.lock().unwrap() {
            break;
        }
        
        // GPT-5 Hot-Thread Fix: Noch längere Pause zwischen Cleanup-Zyklen (60 Sekunden)
        tokio::time::sleep(Duration::from_secs(60)).await;
        
        match cleanup_expired_files(&output_dir, ttl_duration).await {
            Ok(deleted_count) => {
                if deleted_count > 0 {
                    info!("🧹 Cleanup completed: deleted {} expired files", deleted_count);
                } else {
                    info!("🧹 Cleanup completed: no expired files found");
                }
            }
            Err(e) => {
                warn!("⚠️  Cleanup failed: {}", e);
            }
        }
    }
    
    info!("🛑 TTL cleanup task stopped");
}

/// Löscht alle .raw und .json Dateien, die älter als die TTL sind
async fn cleanup_expired_files(
    output_dir: &PathBuf,
    ttl_duration: Duration,
) -> Result<usize, Box<dyn std::error::Error + Send + Sync>> {
    let now = SystemTime::now();
    let mut deleted_count = 0;
    
    let mut entries = tokio_fs::read_dir(output_dir).await?;
    
    while let Some(entry) = entries.next_entry().await? {
        let path = entry.path();
        
        // Nur .raw und .json Dateien berücksichtigen
        if let Some(extension) = path.extension() {
            let ext = extension.to_string_lossy();
            if ext != "raw" && ext != "json" {
                continue;
            }
        } else {
            continue;
        }
        
        // latest.raw Symlink nicht löschen
        if path.file_name().unwrap_or_default() == "latest.raw" {
            continue;
        }
        
        // Prüfe Dateialter
        match entry.metadata().await {
            Ok(metadata) => {
                if let Ok(modified) = metadata.modified() {
                    if let Ok(age) = now.duration_since(modified) {
                        if age > ttl_duration {
                            // Datei ist zu alt - löschen
                            match tokio_fs::remove_file(&path).await {
                                Ok(_) => {
                                    info!("🗑️  Deleted expired file: {:?} (age: {}min)", 
                                          path.file_name().unwrap_or_default(),
                                          age.as_secs() / 60);
                                    deleted_count += 1;
                                }
                                Err(e) => {
                                    warn!("⚠️  Failed to delete {:?}: {}", path, e);
                                }
                            }
                        }
                    }
                }
            }
            Err(e) => {
                warn!("⚠️  Failed to read metadata for {:?}: {}", path, e);
            }
        }
    }
    
    Ok(deleted_count)
}


/// Fetch preconf from HTTP endpoint (similar to Go implementation)
async fn fetch_http_preconf(
    client: &reqwest::Client,
    endpoint: &str,
) -> Result<Option<(String, serde_json::Value)>, Box<dyn std::error::Error + Send + Sync>> {
    let response = client
        .get(endpoint)
        .timeout(Duration::from_secs(10))
        .send()
        .await?;
    
    if !response.status().is_success() {
        return Err(format!("HTTP {} from {}", response.status(), endpoint).into());
    }
    
    let body = response.text().await?;
    let data_hash = format!("{:x}", md5::compute(&body));
    
    let preconf: serde_json::Value = serde_json::from_str(&body)?;
    
    Ok(Some((data_hash, preconf)))
}

/// Process HTTP preconf and save to filesystem (similar to Go implementation)
async fn process_http_preconf(
    preconf: serde_json::Value,
    chain_id: u64,
    output_dir: &PathBuf,
) -> Result<u64, Box<dyn std::error::Error + Send + Sync>> {
    // Extract data and signature from JSON
    let data_hex = preconf["data"]
        .as_str()
        .ok_or("Missing 'data' field")?;
    let signature = &preconf["signature"];
    
    // Decode hex data
    let data_bytes = hex::decode(data_hex.trim_start_matches("0x"))?;
    
    // Create signature bytes (r + s + v)
    let r_hex = signature["r"].as_str().ok_or("Missing signature.r")?;
    let s_hex = signature["s"].as_str().ok_or("Missing signature.s")?;
    let y_parity = signature["yParity"].as_str().unwrap_or("0x0");
    
    let r_bytes = hex::decode(r_hex.trim_start_matches("0x"))?;
    let s_bytes = hex::decode(s_hex.trim_start_matches("0x"))?;
    let v_byte = if y_parity == "0x1" { 28 } else { 27 };
    
    // Pad r and s to 32 bytes and create signature
    let mut sig_bytes = [0u8; 65];
    sig_bytes[32 - r_bytes.len()..32].copy_from_slice(&r_bytes);
    sig_bytes[64 - s_bytes.len()..64].copy_from_slice(&s_bytes);
    sig_bytes[64] = v_byte;
    
    // Extract block number (mock for now - would need proper SSZ parsing)
    let block_number = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    
        // Compress HTTP preconfs with ZSTD (level 1 for speed)
        let compressed = zstd::bulk::compress(&data_bytes, 1)?;
    
    // Combine compressed payload + signature
    let mut combined = compressed;
    combined.extend_from_slice(&sig_bytes);
    
    // Save to filesystem
    let filename = format!("block_{}_{}.raw", chain_id, block_number);
    let filepath = output_dir.join(&filename);
    tokio_fs::write(&filepath, &combined).await?;
    
    // Update symlinks (latest.raw and pre_latest.raw) 
    update_symlinks_lib(output_dir, &filename, chain_id).await?;
    
    Ok(block_number)
}

/// Update both latest.raw and pre_latest.raw symlinks for lib.rs
async fn update_symlinks_lib(
    output_dir: &PathBuf,
    new_latest_filename: &str,
    chain_id: u64,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let latest_path = output_dir.join("latest.raw");
    let pre_latest_path = output_dir.join("pre_latest.raw");
    
    // Find the previous latest file by scanning directory
    let previous_filename = find_previous_block_file(output_dir, chain_id).await?;
    
    // Update pre_latest.raw first (if we have a previous file)
    if let Some(prev_filename) = previous_filename {
        // Remove old pre_latest symlink
        let _ = tokio_fs::remove_file(&pre_latest_path).await; // Ignore error if doesn't exist
        
        // Create new pre_latest symlink
        tokio_fs::symlink(&prev_filename, &pre_latest_path).await
            .map_err(|e| format!("Failed to create pre_latest.raw symlink: {}", e))?;
        
        info!("🔗 Updated pre_latest.raw symlink to {}", prev_filename);
    }
    
    // Update latest.raw
    let _ = tokio_fs::remove_file(&latest_path).await; // Ignore error if doesn't exist
    tokio_fs::symlink(new_latest_filename, &latest_path).await
        .map_err(|e| format!("Failed to create latest.raw symlink: {}", e))?;
    
    info!("🔗 Updated latest.raw symlink to {}", new_latest_filename);
    
    Ok(())
}

/// Find the previous block file (second newest) by scanning directory
/// Only considers real block numbers, not timestamp-based fake numbers
async fn find_previous_block_file(
    output_dir: &PathBuf,
    chain_id: u64,
) -> Result<Option<String>, Box<dyn std::error::Error + Send + Sync>> {
    let mut block_files = Vec::new();
    
    // Scan directory for block files
    let mut entries = tokio_fs::read_dir(output_dir).await?;
    while let Some(entry) = entries.next_entry().await? {
        let filename = entry.file_name().to_string_lossy().to_string();
        if filename.starts_with(&format!("block_{}_", chain_id)) && filename.ends_with(".raw") {
            // Extract block number
            if let Some(block_part) = filename.strip_prefix(&format!("block_{}_", chain_id)) {
                if let Some(number_str) = block_part.strip_suffix(".raw") {
                    if let Ok(block_number) = number_str.parse::<u64>() {
                        // Filter out timestamp-based fake block numbers
                        // Real block numbers are typically < 100M, timestamps are > 1B
                        if block_number < 1_000_000_000 {
                            block_files.push((block_number, filename));
                        } else {
                            info!("🔍 Ignoring timestamp-based block file: {} (block_number: {})", filename, block_number);
                        }
                    }
                }
            }
        }
    }
    
    if block_files.len() < 2 {
        // Need at least 2 files to have a previous one
        return Ok(None);
    }
    
    // Sort by block number and get the second newest (previous)
    block_files.sort_by_key(|&(number, _)| number);
    let previous_file = &block_files[block_files.len() - 2].1; // Second newest
    
    info!("🔍 Found previous block file: {} (from {} real block files)", previous_file, block_files.len());
    Ok(Some(previous_file.clone()))
}

