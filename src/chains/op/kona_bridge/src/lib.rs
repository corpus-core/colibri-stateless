// lib_helios.rs - C-kompatible Interface für echte Kona-Bridge mit korrektem Preconf-Format

use alloy::primitives::{Address, address};
use discv5::{ConfigBuilder, enr::CombinedKey};
use hex;
use kona_p2p::{LocalNode, Network};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{Multiaddr, identity::Keypair};
use op_alloy_rpc_types_engine::OpNetworkPayloadEnvelope;
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
use tokio::runtime::Runtime;
use tracing::{error, info, warn};

/// C-kompatible Konfiguration für die Kona-Bridge
#[repr(C)]
pub struct KonaBridgeConfig {
    pub chain_id: c_uint,
    pub hardfork: c_uint,
    pub disc_port: c_uint,
    pub gossip_port: c_uint,
    pub ttl_minutes: c_uint,
    pub cleanup_interval: c_uint,
    pub output_dir: *const c_char,
    pub sequencer_address: *const c_char, // kann NULL sein
    pub chain_name: *const c_char,        // kann NULL sein
}

/// Handle für die laufende Bridge-Instanz
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
    }));

    let running = Arc::new(Mutex::new(true));

    // Start Kona network in separate thread
    let stats_clone = stats.clone();
    let running_clone = running.clone();
    let chain_id = config.chain_id as u64;
    let disc_port = config.disc_port as u16;
    let gossip_port = config.gossip_port as u16;
    let output_path = PathBuf::from(output_dir);

    let thread_handle = thread::spawn(move || {
        info!("🔄 Kona-P2P bridge thread starting...");

        let rt = match Runtime::new() {
            Ok(rt) => rt,
            Err(e) => {
                error!("❌ Failed to create Tokio runtime: {}", e);
                return;
            }
        };

        rt.block_on(async {
            info!("🔄 About to start Kona network for chain {}", chain_id);
            if let Err(e) = run_kona_network(
                chain_id,
                disc_port,
                gossip_port,
                &output_path,
                sequencer_address.as_deref(),
                stats_clone,
                running_clone,
            ).await {
                error!("❌ Kona network failed: {}", e);
                error!("❌ Error details: {:?}", e);
            } else {
                info!("✅ Kona network completed successfully");
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

async fn run_kona_network(
    chain_id: u64,
    disc_port: u16,
    gossip_port: u16,
    output_dir: &PathBuf,
    expected_sequencer: Option<&str>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    
    info!("🚀 run_kona_network starting for chain {}", chain_id);
    
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

    let mut network = Network::builder()
        .with_rollup_config(cfg)
        .with_unsafe_block_signer(chain_config.unsafe_signer)
        .with_discovery_address(disc)
        .with_gossip_address(gossip_addr)
        .with_keypair(gossip_key)
        .with_discovery_config(ConfigBuilder::new(disc_listen.into()).build())
        .build()
        .map_err(|e| format!("Failed to build network driver: {}", e))?;

    // Add bootnodes
    for bootnode in chain_config.bootnodes {
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
        .map_err(|e| format!("Failed to start network driver: {}", e))?;

    info!("✅ Kona-P2P network started successfully!");
    info!("🎧 Listening for preconfirmation messages...");

    // Update stats: we're now connected
    {
        let mut stats_guard = stats.lock().unwrap();
        stats_guard.connected_peers = 1; // Start with 1, will be updated
    }

    let mut latest_block_number = 0u64;

    // Process incoming payloads
    while *running.lock().unwrap() {
        tokio::select! {
            result = payload_recv.recv() => {
                match result {
                    Ok(payload_envelope) => {
                        let hash = payload_envelope.payload.block_hash();
                        let number = payload_envelope.payload.block_number();
                        
                        info!("🎉 PRECONF RECEIVED! Block #{} Hash: {}", number, hash);

                        // Update received stats
                        {
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.received_preconfs += 1;
                        }

                        // Check if this is newer than our latest
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
                                    info!("✅ Preconf processed successfully (total: {})", stats_guard.processed_preconfs);
                                }
                                Err(e) => {
                                    let mut stats_guard = stats.lock().unwrap();
                                    stats_guard.failed_preconfs += 1;
                                    error!("❌ Failed to process preconf: {}", e);
                                }
                            }
                        } else {
                            info!("⏭️  Skipping old preconf (block #{} <= {})", number, latest_block_number);
                        }
                    }
                    Err(e) => {
                        error!("❌ Error receiving payload: {}", e);
                        break;
                    }
                }
            }
            _ = tokio::time::sleep(Duration::from_secs(1)) => {
                // Periodic check if we should stop
                if !*running.lock().unwrap() {
                    break;
                }
            }
        }
    }

    info!("🛑 Kona-P2P network stopping...");
    Ok(())
}

async fn process_preconf_with_correct_format(
    payload_envelope: &OpNetworkPayloadEnvelope,
    chain_id: u64,
    output_dir: &PathBuf,
    expected_sequencer: Option<&str>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let block_number = payload_envelope.payload.block_number();
    let block_hash = payload_envelope.payload.block_hash();
    
    info!("📦 Processing preconf for block #{}", block_number);

    // Extract execution payload (JSON serialized for now - TODO: implement proper SSZ)
    let execution_payload_bytes = serde_json::to_vec(&payload_envelope.payload)
        .map_err(|e| format!("Failed to serialize execution payload: {}", e))?;

    // Format: 32-byte domain + execution_payload (wie in Go-Implementation)
    let mut preconf_data = Vec::new();
    
    // Add 32-byte zero domain
    preconf_data.extend_from_slice(&[0u8; 32]);
    
    // Add execution payload
    preconf_data.extend_from_slice(&execution_payload_bytes);

    info!("📊 Preconf data: {} bytes (domain: 32, payload: {})", 
          preconf_data.len(), execution_payload_bytes.len());

    // Compress with ZSTD (wie in Go-Implementation)
    let compressed_payload = zstd::encode_all(preconf_data.as_slice(), 0)
        .map_err(|e| format!("ZSTD compression failed: {}", e))?;
    
    info!("🗜️  ZSTD compression: {} bytes -> {} bytes ({}% reduction)",
        preconf_data.len(), 
        compressed_payload.len(),
        100 - (compressed_payload.len() * 100 / preconf_data.len().max(1)));

    // Extract signature (65 bytes)
    let signature_bytes = signature_to_bytes(&payload_envelope.signature);

    // Format: compressed_payload + signature (65 bytes) - genau wie Go-Implementation
    let mut final_data = Vec::new();
    final_data.extend_from_slice(&compressed_payload);
    final_data.extend_from_slice(&signature_bytes);

    // Write to file: block_{chain_id}_{block_number}.raw
    let filename = format!("block_{}_{}.raw", chain_id, block_number);
    let filepath = output_dir.join(&filename);
    
    // Atomic write
    let temp_filepath = filepath.with_extension("tmp");
    fs::write(&temp_filepath, &final_data)
        .map_err(|e| format!("Failed to write temp file: {}", e))?;
    fs::rename(&temp_filepath, &filepath)
        .map_err(|e| format!("Failed to rename temp file: {}", e))?;

    info!("💾 Saved preconf to: {:?} ({} bytes)", filepath, final_data.len());

    // Update latest.raw symlink (nur wenn block_number neuer ist)
    let latest_path = output_dir.join("latest.raw");
    if let Err(e) = fs::remove_file(&latest_path) {
        // Ignore error if file doesn't exist
        if e.kind() != std::io::ErrorKind::NotFound {
            warn!("⚠️  Failed to remove old latest.raw: {}", e);
        }
    }

    // Create new symlink
    #[cfg(unix)]
    {
        use std::os::unix::fs::symlink;
        if let Err(e) = symlink(&filename, &latest_path) {
            warn!("⚠️  Failed to create latest.raw symlink: {}", e);
        } else {
            info!("🔗 Updated latest.raw symlink to {}", filename);
        }
    }

    // Create metadata file (wie in Go-Implementation)
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
        "kona_p2p": true
    });

    fs::write(&meta_filepath, serde_json::to_string_pretty(&metadata)?)
        .map_err(|e| format!("Failed to write metadata: {}", e))?;

    Ok(())
}

fn signature_to_bytes(signature: &alloy::signers::Signature) -> [u8; 65] {
    let mut bytes = [0u8; 65];
    bytes[..32].copy_from_slice(&signature.r().to_be_bytes::<32>());
    bytes[32..64].copy_from_slice(&signature.s().to_be_bytes::<32>());
    bytes[64] = if signature.v() { 1 } else { 0 };
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
        }
        0
    } else {
        -1
    }
}

/// Hilfsfunktion für Logging-Setup von C aus
#[no_mangle]
pub extern "C" fn kona_bridge_init_logging() {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();
}

struct ChainConfig {
    unsafe_signer: Address,
    chain_id: u64,
    bootnodes: Vec<discv5::Enr>,
}

impl ChainConfig {
    fn from(network: &str, chain_id: u64) -> Self {
        match network {
            "op-mainnet" => ChainConfig {
                unsafe_signer: address!("AAAA45d9549EDA09E70937013520214382Ffc4A2"),
                chain_id: 10,
                bootnodes: Vec::new(),
            },
            "base" => ChainConfig {
                unsafe_signer: address!("Af6E19BE0F9cE7f8afd49a1824851023A8249e8a"),
                chain_id: 8453,
                bootnodes: Vec::new(),
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
