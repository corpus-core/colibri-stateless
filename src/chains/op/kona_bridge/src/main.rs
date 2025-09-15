// main_helios.rs - Echte Kona-P2P Implementation basierend auf Helios
use alloy::{
    primitives::{Address, Bytes, address, B256},
    signers::Signature,
};
use clap::Parser;
use discv5::{ConfigBuilder, Enr, enr::CombinedKey};
use kona_p2p::{LocalNode, Network};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{Multiaddr, identity::Keypair};
use op_alloy_rpc_types_engine::{OpExecutionPayload, OpNetworkPayloadEnvelope};
use serde::{Deserialize, Serialize};
use ethereum_ssz::Encode;
use std::{
    borrow::BorrowMut,
    fs,
    net::{IpAddr, Ipv4Addr, SocketAddr},
    path::PathBuf,
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};
use tokio::sync::RwLock;
use tracing::{info, warn, error};
use tracing_subscriber::{EnvFilter, FmtSubscriber};

#[derive(Parser)]
#[command(name = "kona_bridge")]
#[command(about = "Kona-P2P OP Stack Preconfirmation Bridge")]
struct Args {
    /// Chain ID (z.B. 10 für OP Mainnet, 8453 für Base)
    #[arg(short, long, default_value = "8453")]
    chain_id: u64,

    /// Network name (op-mainnet, base, unichain, etc.)
    #[arg(short, long, default_value = "base")]
    network: String,

    /// Output-Verzeichnis für Preconfs
    #[arg(short, long, default_value = "./preconfs")]
    output_dir: PathBuf,

    /// Discovery Port
    #[arg(long, default_value = "9090")]
    disc_port: u16,

    /// Gossip Port  
    #[arg(long, default_value = "9091")]
    gossip_port: u16,

    /// Expected Sequencer Address (optional)
    #[arg(long)]
    sequencer_address: Option<String>,

    /// Chain Name (optional, für Logging)
    #[arg(long)]
    chain_name: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    enable_tracing();
    let args = Args::parse();

    info!("🚀 Starting Kona-P2P OP Stack Bridge");
    info!("⛓️  Chain ID: {}", args.chain_id);
    info!("🌐 Network: {}", args.network);
    info!("📁 Output: {:?}", args.output_dir);

    // Create output directory
    fs::create_dir_all(&args.output_dir)?;

    // Start network
    start_network(&args).await?;

    Ok(())
}

async fn start_network(args: &Args) -> Result<(), Box<dyn std::error::Error>> {
    let chain_config = ChainConfig::from(args.network.as_str(), args.chain_id);

    let gossip = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)), args.gossip_port);
    let mut gossip_addr = Multiaddr::from(gossip.ip());
    gossip_addr.push(libp2p::multiaddr::Protocol::Tcp(gossip.port()));

    let CombinedKey::Secp256k1(k256_key) = CombinedKey::generate_secp256k1() else {
        return Err("Failed to generate secp256k1 key".into());
    };
    
    let advertise_ip = IpAddr::V4(Ipv4Addr::UNSPECIFIED);
    let disc = LocalNode::new(k256_key, advertise_ip, args.disc_port, args.disc_port);
    let disc_listen = SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), args.disc_port);

    let gossip_key = Keypair::generate_secp256k1();

    let cfg = ROLLUP_CONFIGS
        .get(&chain_config.chain_id)
        .ok_or_else(|| format!("Rollup config not found for chain {}", chain_config.chain_id))?
        .clone();

    info!("🔍 Discovery: 0.0.0.0:{}", args.disc_port);
    info!("📡 Gossip: 0.0.0.0:{}", args.gossip_port);
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

    let state = Arc::new(RwLock::new(PreconfState {
        latest_block_number: 0,
        previous_block_number: None,
        processed_count: 0,
    }));

    let output_dir = args.output_dir.clone();
    let chain_id = args.chain_id;
    let expected_sequencer = args.sequencer_address.clone();

    // Process incoming payloads
    while let Ok(payload_envelope) = payload_recv.recv().await {
        let hash = payload_envelope.payload.block_hash();
        let number = payload_envelope.payload.block_number();
        
        info!("🎉 PRECONF RECEIVED! Block #{} Hash: {}", number, hash);

        // Check if this is newer than our latest
        let should_process = {
            let state_guard = state.read().await;
            number > state_guard.latest_block_number
        };

        if should_process {
            match process_preconf(&payload_envelope, chain_id, &output_dir, expected_sequencer.as_deref()).await {
                Ok(()) => {
                    let mut state_guard = state.write().await;
                    
                    // Update symlinks with atomic operation
                    let filename = format!("block_{}_{}.raw", chain_id, number);
                    let current_latest = state_guard.latest_block_number;
                    
                    info!("🔄 Updating symlinks: new={}, previous_block={}", filename, current_latest);
                    
                    if let Err(e) = update_symlinks(&output_dir, &filename, current_latest, chain_id).await {
                        error!("⚠️  Failed to update symlinks: {}", e);
                    }
                    
                    // Update state
                    if current_latest > 0 {
                        state_guard.previous_block_number = Some(current_latest);
                    }
                    state_guard.latest_block_number = number;
                    state_guard.processed_count += 1;
                    info!("✅ Preconf processed successfully (total: {})", state_guard.processed_count);
                }
                Err(e) => {
                    error!("❌ Failed to process preconf: {}", e);
                }
            }
        } else {
            info!("⏭️  Skipping old preconf (block #{} <= {})", number, state.read().await.latest_block_number);
        }
    }

    Ok(())
}

async fn process_preconf(
    payload_envelope: &OpNetworkPayloadEnvelope,
    chain_id: u64,
    output_dir: &PathBuf,
    expected_sequencer: Option<&str>,
) -> Result<(), Box<dyn std::error::Error>> {
    let block_number = payload_envelope.payload.block_number();
    let block_hash = payload_envelope.payload.block_hash();
    
    info!("📦 Processing preconf for block #{}", block_number);

    // Create SequencerCommitment (same as Helios)
    let commitment = SequencerCommitment::from(payload_envelope.clone());
    
    // Extract execution payload
    let execution_payload_bytes = match &payload_envelope.payload {
        OpExecutionPayload::V1(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V2(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V3(payload) => payload.as_ssz_bytes(),
        OpExecutionPayload::V4(payload) => payload.as_ssz_bytes(),
    };

    // Verify sequencer signature if expected sequencer is provided
    if let Some(expected) = expected_sequencer {
        let recovered_address = recover_sequencer_address(&commitment, chain_id)?;
        let expected_address = expected.parse::<Address>()
            .map_err(|e| format!("Invalid expected sequencer address: {}", e))?;
        
        if recovered_address != expected_address {
            return Err(format!(
                "Sequencer mismatch: expected {}, got {}", 
                expected_address, recovered_address
            ).into());
        }
        
        info!("🔐 Sequencer signature verified: {}", recovered_address);
    }

    // Format: 32-byte domain + execution_payload
    let mut preconf_data = Vec::new();
    
    // Add 32-byte zero domain (wie in Go-Implementation)
    preconf_data.extend_from_slice(&[0u8; 32]);
    
    // Add execution payload
    preconf_data.extend_from_slice(&execution_payload_bytes);

    // Compress with ZSTD
    let compressed_payload = zstd::encode_all(preconf_data.as_slice(), 0)?;
    
    info!("🗜️  ZSTD compression: {} bytes -> {} bytes ({}% reduction)",
        preconf_data.len(), 
        compressed_payload.len(),
        100 - (compressed_payload.len() * 100 / preconf_data.len()));

    // Extract signature (65 bytes)
    let signature_bytes = signature_to_bytes(&commitment.signature);

    // Combine: compressed_payload + signature (65 bytes)
    let mut final_data = Vec::new();
    final_data.extend_from_slice(&compressed_payload);
    final_data.extend_from_slice(&signature_bytes);

    // Write to file: block_{chain_id}_{block_number}.raw
    let filename = format!("block_{}_{}.raw", chain_id, block_number);
    let filepath = output_dir.join(&filename);
    
    // Atomic write
    let temp_filepath = filepath.with_extension("tmp");
    fs::write(&temp_filepath, &final_data)?;
    fs::rename(&temp_filepath, &filepath)?;

    info!("💾 Saved preconf to: {:?}", filepath);

    // Create metadata file
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
        "file_path": filename
    });

    fs::write(&meta_filepath, serde_json::to_string_pretty(&metadata)?)?;

    Ok(())
}

fn signature_to_bytes(signature: &Signature) -> [u8; 65] {
    let mut bytes = [0u8; 65];
    bytes[..32].copy_from_slice(&signature.r().to_be_bytes::<32>());
    bytes[32..64].copy_from_slice(&signature.s().to_be_bytes::<32>());
    bytes[64] = if signature.v() { 28 } else { 27 };
    bytes
}

fn recover_sequencer_address(commitment: &SequencerCommitment, chain_id: u64) -> Result<Address, Box<dyn std::error::Error>> {
    // TODO: Implement signature recovery logic
    // This would use the same logic as in the Go implementation
    Ok(Address::ZERO) // Placeholder
}

fn enable_tracing() {
    let env_filter = EnvFilter::from_default_env()
        .add_directive("kona_bridge".parse().unwrap());

    let subscriber = FmtSubscriber::builder()
        .with_env_filter(env_filter)
        .finish();

    tracing::subscriber::set_global_default(subscriber)
        .expect("subscriber set failed");
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct SequencerCommitment {
    data: Bytes,
    signature: Signature,
}

impl From<OpNetworkPayloadEnvelope> for SequencerCommitment {
    fn from(value: OpNetworkPayloadEnvelope) -> Self {
        let parent_root = value.parent_beacon_block_root.unwrap_or_default();
        let payload = match value.payload {
            OpExecutionPayload::V1(payload) => payload.as_ssz_bytes(),
            OpExecutionPayload::V2(payload) => payload.as_ssz_bytes(),
            OpExecutionPayload::V3(payload) => payload.as_ssz_bytes(),
            OpExecutionPayload::V4(payload) => payload.as_ssz_bytes(),
        };

        let data = [parent_root.as_slice(), &payload].concat();

        SequencerCommitment {
            data: data.into(),
            signature: value.signature,
        }
    }
}

struct PreconfState {
    latest_block_number: u64,
    previous_block_number: Option<u64>,
    processed_count: u64,
}

struct ChainConfig {
    unsafe_signer: Address,
    chain_id: u64,
    bootnodes: Vec<Enr>,
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
            _ => {
                // Use provided chain_id for custom networks
                ChainConfig {
                    unsafe_signer: Address::ZERO, // Will need to be configured
                    chain_id,
                    bootnodes: Vec::new(),
                }
            }
        }
    }
}


async fn update_symlinks(
    output_dir: &PathBuf, 
    new_latest_filename: &str, 
    previous_block_number: u64,
    chain_id: u64
) -> Result<(), Box<dyn std::error::Error>> {
    let latest_path = output_dir.join("latest.raw");
    let pre_latest_path = output_dir.join("pre_latest.raw");
    
    #[cfg(unix)]
    {
        use std::os::unix::fs::symlink;
        
        // Update pre_latest.raw first (should point to current latest before we update it)
        if previous_block_number > 0 {
            let previous_filename = format!("block_{}_{}.raw", chain_id, previous_block_number);
            let previous_file_path = output_dir.join(&previous_filename);
            
            info!("🔍 Checking for previous block file: {} (exists: {})", previous_filename, previous_file_path.exists());
            
            // Only update pre_latest if the previous file actually exists
            if previous_file_path.exists() {
                // Remove old pre_latest symlink
                if let Err(e) = fs::remove_file(&pre_latest_path) {
                    if e.kind() != std::io::ErrorKind::NotFound {
                        warn!("⚠️  Failed to remove old pre_latest.raw: {}", e);
                    }
                }
                
                // Create new pre_latest symlink
                if let Err(e) = symlink(&previous_filename, &pre_latest_path) {
                    warn!("⚠️  Failed to create pre_latest.raw symlink: {}", e);
                } else {
                    info!("🔗 Updated pre_latest.raw symlink to {}", previous_filename);
                }
            } else {
                warn!("⚠️  Previous block file {} does not exist, skipping pre_latest.raw update", previous_filename);
            }
        }
        
        // Update latest.raw
        // Remove old latest symlink
        if let Err(e) = fs::remove_file(&latest_path) {
            if e.kind() != std::io::ErrorKind::NotFound {
                warn!("⚠️  Failed to remove old latest.raw: {}", e);
            }
        }
        
        // Create new latest symlink
        if let Err(e) = symlink(new_latest_filename, &latest_path) {
            warn!("⚠️  Failed to create latest.raw symlink: {}", e);
        } else {
            info!("🔗 Updated latest.raw symlink to {}", new_latest_filename);
        }
        
        // Verify both symlinks exist and are valid
        verify_symlinks(output_dir).await?;
    }
    
    Ok(())
}

async fn verify_symlinks(output_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let latest_path = output_dir.join("latest.raw");
    let pre_latest_path = output_dir.join("pre_latest.raw");
    
    // Check latest.raw
    if !latest_path.exists() {
        warn!("⚠️  latest.raw symlink does not exist!");
        return Err("latest.raw symlink missing".into());
    }
    
    // Check if latest.raw points to a valid file
    match fs::metadata(&latest_path) {
        Ok(metadata) => {
            if !metadata.is_file() {
                warn!("⚠️  latest.raw does not point to a valid file!");
            } else {
                info!("✅ latest.raw symlink is valid");
            }
        }
        Err(e) => {
            warn!("⚠️  latest.raw symlink is broken: {}", e);
        }
    }
    
    // Check pre_latest.raw (optional, might not exist for first block )
    if pre_latest_path.exists() {
        match fs::metadata(&pre_latest_path) {
            Ok(metadata) => {
                if !metadata.is_file() {
                    warn!("⚠️  pre_latest.raw does not point to a valid file!");
                } else {
                    info!("✅ pre_latest.raw symlink is valid");
                }
            }
            Err(e) => {
                warn!("⚠️  pre_latest.raw symlink is broken: {}", e);
            }
        }
    } else {
        info!("ℹ️  pre_latest.raw does not exist (normal for first block)");
    }
    
    Ok(())
}
