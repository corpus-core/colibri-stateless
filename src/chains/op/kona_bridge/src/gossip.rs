// gossip.rs - Signature-preserving Optimism gossip loop built on kona-node v1.2.8

use crate::{
    config::ChainConfig,
    processing::process_preconf_with_correct_format,
    types::{BlockBitmaskTracker, BlockDeduplicator, KonaBridgeStats},
};
use discv5::{ConfigBuilder, ListenConfig, enr::CombinedKey};
use kona_disc::{Discv5Builder, LocalNode};
use kona_gossip::GossipDriverBuilder;
use kona_peers::{BootNode, BootNodes};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{Multiaddr, identity::Keypair};
use std::{
    net::{IpAddr, Ipv4Addr},
    path::PathBuf,
    sync::{Arc, Mutex},
    time::Duration,
};
use tokio::sync::broadcast;
use tracing::{error, info, warn};

/// Runs the preconf gossip loop until `running` is toggled off or the network shuts down.
///
/// The loop drives kona's split `Discv5Driver` and `GossipDriver` directly (skipping
/// `NetworkActor`/`NetworkDriver` in `kona-node-service`) because:
///   1. Only `GossipDriver::handle_event` exposes the signed `OpNetworkPayloadEnvelope`;
///      the actor's `blocks` channel loses the P2P signature via
///      `OpNetworkPayloadEnvelope -> OpExecutionPayloadEnvelope`, which colibri writes into
///      the `.raw` preconf files via `process_preconf_with_correct_format`.
///   2. Depending on `kona-node-service` transitively pulls in `rollup-boost`, whose build
///      script cannot compile under the current `vergen-git2`/`vergen-lib` version graph.
///
/// Processing (`process_preconf_with_correct_format`) is awaited inline on the same task
/// that drives `gossip.next()`. This mirrors kona's own `NetworkActor` shape and is fine as
/// long as processing stays cheap (roughly: one `spawn_blocking` zstd + a few async file
/// writes, i.e. sub-block-time on OP chains). If future work makes processing heavier, move
/// it onto a dedicated task with a bounded channel to avoid stalling the swarm.
pub async fn run_gossip_network(
    chain_id: u64,
    disc_port: u16,
    gossip_port: u16,
    output_dir: &PathBuf,
    chain_config: &ChainConfig,
    expected_sequencer: Option<&str>,
    stats: Arc<Mutex<KonaBridgeStats>>,
    running: Arc<Mutex<bool>>,
    deduplicator: Option<Arc<Mutex<BlockDeduplicator>>>,
    bitmask_tracker: Option<Arc<Mutex<BlockBitmaskTracker>>>,
    sse_tx: Option<broadcast::Sender<u64>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let mut gossip_addr = Multiaddr::from(IpAddr::V4(Ipv4Addr::UNSPECIFIED));
    gossip_addr.push(libp2p::multiaddr::Protocol::Tcp(gossip_port));

    let CombinedKey::Secp256k1(k256_key) = CombinedKey::generate_secp256k1() else {
        return Err("Failed to generate secp256k1 key".into());
    };

    let disc_ip = Ipv4Addr::UNSPECIFIED;
    let discovery_address = LocalNode::new(k256_key, IpAddr::V4(disc_ip), disc_port, disc_port);
    let discovery_config =
        ConfigBuilder::new(ListenConfig::Ipv4 { ip: disc_ip, port: disc_port }).build();

    let gossip_keypair = Keypair::generate_secp256k1();

    tracing::debug!("🔍 Looking up rollup config for chain {}", chain_config.chain_id);
    let rollup_config = ROLLUP_CONFIGS
        .get(&chain_config.chain_id)
        .or_else(|| {
            warn!(
                "⚠️  Rollup config not found for chain {}, using Base as fallback",
                chain_config.chain_id
            );
            ROLLUP_CONFIGS.get(&8453) // Base fallback
        })
        .ok_or_else(|| {
            format!("No rollup config found for chain {} or Base fallback", chain_config.chain_id)
        })?
        .clone();
    tracing::debug!("✅ Found rollup config for chain {}", chain_config.chain_id);

    tracing::debug!("🔍 Discovery: 0.0.0.0:{}", disc_port);
    tracing::debug!("📡 Gossip: 0.0.0.0:{}", gossip_port);
    tracing::debug!("🔐 Expected sequencer: {}", chain_config.unsafe_signer);

    // Take the first 3 configured bootnodes to keep the initial DHT bootstrap set small;
    // kona-disc will still union these with the built-in per-chain bootnodes.
    let initial_bootnodes: Vec<_> = chain_config
        .bootnodes
        .iter()
        .take(3)
        .cloned()
        .map(BootNode::from)
        .collect();
    tracing::debug!(
        "🔧 P2P bootstrap: using {} configured bootnode(s) for initial discovery",
        initial_bootnodes.len()
    );

    let l2_chain_id = rollup_config.l2_chain_id.id();

    // Build the gossip driver (produces signed OpNetworkPayloadEnvelopes via handle_event).
    // The returned `unsafe_block_signer_sender` seeds a `watch::channel` inside the driver
    // with `chain_config.unsafe_signer` and is retained here on purpose: we do not rotate
    // the signer at runtime, and `watch` preserves the last value for readers even after
    // its sender is dropped. Binding it keeps the intent explicit.
    let (mut gossip, _unsafe_block_signer_sender) = GossipDriverBuilder::new(
        rollup_config,
        chain_config.unsafe_signer,
        gossip_addr,
        gossip_keypair,
    )
    .build()
    .map_err(|e| format!("Failed to build gossip driver: {}", e))?;

    // Start listening on the gossip multiaddr before wiring up discovery.
    gossip
        .start()
        .await
        .map_err(|e| format!("Failed to start gossip swarm: {}", e))?;

    // Build and spawn the discovery driver. `start()` returns a handle plus an ENR
    // receiver channel that the gossip loop dials into as peers show up. The handle owns
    // the request-mpsc sender that keeps the internal driver task alive; dropping it early
    // would cause kona's driver to hot-spin on the closed channel, so we bind it for the
    // full lifetime of this function.
    let discovery = Discv5Builder::new(discovery_address, l2_chain_id, discovery_config)
        .with_bootnodes(BootNodes(initial_bootnodes))
        .build()
        .map_err(|e| format!("Failed to build discovery driver: {}", e))?;
    let (_disc_handler, mut enr_receiver) = discovery.start();

    info!("📡 Gossip network started - listening for blocks...");

    let mut latest_block_number = 0u64;
    let mut last_status_block = 0u64;
    const STATUS_INTERVAL: u64 = 200; // Status every ~200 blocks (~6 min on OP chains)

    // Poll the shutdown flag on a coarse interval so the select! branches wake up
    // regularly and can honour a stop request without busy-waiting.
    let mut shutdown_check = tokio::time::interval(Duration::from_secs(1));

    loop {
        tokio::select! {
            _ = shutdown_check.tick() => {
                if !*running.lock().unwrap() {
                    break;
                }
            }
            event = gossip.next() => {
                let Some(event) = event else {
                    error!("🛑 GOSSIP: swarm stream ended");
                    break;
                };
                let Some(payload_envelope) = gossip.handle_event(event) else {
                    continue;
                };

                let hash = payload_envelope.payload.block_hash();
                let number = payload_envelope.payload.block_number();
                tracing::debug!("🎉 P2P: PRECONF RECEIVED! Block #{} Hash: {}", number, hash);

                {
                    let mut stats_guard = stats.lock().unwrap();
                    stats_guard.received_preconfs += 1;
                    stats_guard.gossip_received += 1;
                }

                if number <= latest_block_number {
                    info!("⏭️  GOSSIP: Skipping old preconf #{}", number);
                    continue;
                }

                // Race-condition protection: check the deduplicator first.
                let is_duplicate = if let Some(ref dedup_arc) = deduplicator {
                    let dedup = dedup_arc.lock().unwrap();
                    dedup.is_duplicate(number)
                } else {
                    false
                };
                if is_duplicate {
                    tracing::debug!("🛡️  GOSSIP: Block {} already processed by HTTP - skipping", number);
                    continue;
                }

                // Gap detection is only meaningful after we've accepted at least one block.
                if latest_block_number > 0 {
                    let gap = number.saturating_sub(latest_block_number);
                    if gap > 1 {
                        let missing_blocks = gap - 1;
                        tracing::debug!(
                            "📡 GOSSIP: Real gap detected {} -> {} (missing {} blocks)",
                            latest_block_number, number, missing_blocks
                        );
                        let mut stats_guard = stats.lock().unwrap();
                        stats_guard.gossip_gaps += missing_blocks as u32;
                    }
                }

                match process_preconf_with_correct_format(
                    &payload_envelope,
                    chain_id,
                    output_dir,
                    expected_sequencer,
                ).await {
                    Ok(()) => {
                        latest_block_number = number;
                        if let Some(ref tx) = sse_tx {
                            let _ = tx.send(number);
                        }
                        let mut stats_guard = stats.lock().unwrap();
                        stats_guard.processed_preconfs += 1;
                        stats_guard.gossip_processed += 1;

                        if let Some(ref bitmask_tracker_arc) = bitmask_tracker {
                            let mut tracker = bitmask_tracker_arc.lock().unwrap();
                            tracker.mark_block_processed(number);
                            tracing::debug!("🎯 GOSSIP: Marked block {} in bitmask tracker", number);
                        }

                        if let Some(ref dedup_arc) = deduplicator {
                            let mut dedup = dedup_arc.lock().unwrap();
                            dedup.mark_processed(number);
                        }

                        if number >= last_status_block + STATUS_INTERVAL {
                            let skipped = stats_guard
                                .gossip_received
                                .saturating_sub(stats_guard.gossip_processed);
                            info!(
                                "📡 GOSSIP Status: Block #{}, Total processed: {}, HTTP: {}, Gossip: {} (skipped: {})",
                                number,
                                stats_guard.processed_preconfs,
                                stats_guard.http_processed,
                                stats_guard.gossip_processed,
                                skipped
                            );
                            last_status_block = number;
                        }

                        tracing::debug!("✅ GOSSIP: Processed (total: {})", stats_guard.processed_preconfs);
                    }
                    Err(e) => {
                        let mut stats_guard = stats.lock().unwrap();
                        stats_guard.failed_preconfs += 1;
                        error!("❌ GOSSIP: Failed: {}", e);
                    }
                }
            }
            enr = enr_receiver.recv() => {
                let Some(enr) = enr else {
                    error!("🛑 GOSSIP: ENR receiver closed");
                    break;
                };
                gossip.dial(enr);
            }
        }
    }

    info!("🛑 Gossip network stopping...");
    Ok(())
}
