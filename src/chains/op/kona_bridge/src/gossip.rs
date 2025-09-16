// gossip.rs - P2P-Gossip-Netzwerk für Preconfs

use crate::{
    config::ChainConfig,
    processing::process_preconf_with_correct_format,
    types::KonaBridgeStats,
};
use discv5::{ConfigBuilder, enr::CombinedKey};
use kona_p2p::{LocalNode, Network};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{Multiaddr, identity::Keypair};
use std::{
    borrow::BorrowMut,
    net::{IpAddr, Ipv4Addr, SocketAddr},
    path::PathBuf,
    sync::{Arc, Mutex},
    time::Duration,
};
use tracing::{error, info, warn};

/// Reine Gossip-Netzwerk Implementierung
pub async fn run_gossip_network(
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
