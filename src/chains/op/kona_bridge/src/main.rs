// main.rs - Standalone kona-preconf-service Binary
//
// Startet den P2P-Gossip-Receiver und einen HTTP-Server (Port 8080).
// Konfiguration via Umgebungsvariablen:
//   CHAIN_ID                   (required) OP-Stack Chain-ID, z.B. 8453 für Base
//   OUTPUT_DIR                 Verzeichnis für Preconf-Dateien (default: ./preconfs)
//   DISC_PORT                  discv5 Discovery-Port (default: 9090)
//   GOSSIP_PORT                libp2p Gossip-Port (default: 9091)
//   HTTP_PORT                  HTTP-Server-Port (default: 8080)
//   TTL_MINUTES                TTL für Preconf-Dateien (default: 60)
//   CLEANUP_INTERVAL_MINUTES   Cleanup-Intervall in Minuten (default: 5)
//   MODE                       gossip (default) | http-first
//   HTTP_POLL_INTERVAL         HTTP-Polling-Intervall in Sekunden (default: 1)
//   HTTP_FAILURE_THRESHOLD     Fehler vor Gossip-Fallback in http-first (default: 5)
//   SEQUENCER_ADDRESS          Erwartete Sequencer-Adresse (optional)
//   ADVERTISE_IP               Public/LAN IP written into the discv5 ENR (optional)
//
// A secp256k1 identity is persisted as `{OUTPUT_DIR}/p2p.secp256k1` so discv5
// and libp2p share the same PeerId across restarts.

mod config;
mod gossip;
mod http;
mod processing;
mod server;
mod types;
mod utils;

use alloy_primitives::Address;
use std::{env, fs, path::PathBuf, sync::{Arc, Mutex}};
use tokio::sync::broadcast;
use tracing::{info, warn};
use tracing_subscriber::EnvFilter;

use config::ChainConfig;
use http::run_http_primary_with_gossip_fallback;
use types::{BlockBitmaskTracker, BlockDeduplicator, BridgeMode, HttpHealthTracker, KonaBridgeStats};
use utils::cleanup_old_files;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| {
                // We don't depend on kona-node-service, so no filter for it here.
                // kona_disc/kona_gossip stay at `warn` (not `off`) so startup / listen /
                // discovery-error messages still surface without flooding info-level logs.
                EnvFilter::new(
                    "warn,kona_preconf_service=info,kona_bridge=info,libp2p=off,discv5=off,gossip=warn,discovery=info,kona_gossip=warn,kona_disc=info,kona_peers=warn,hyper=off",
                )
            }),
        )
        .with_target(false)
        .compact()
        .init();

    let chain_id: u64 = env::var("CHAIN_ID")
        .expect("CHAIN_ID env var required")
        .parse()
        .expect("CHAIN_ID must be a number");

    let output_dir = PathBuf::from(
        env::var("OUTPUT_DIR").unwrap_or_else(|_| "./preconfs".to_string()),
    );
    let disc_port: u16 = env::var("DISC_PORT")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(9090);
    let gossip_port: u16 = env::var("GOSSIP_PORT")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(9091);
    let http_port: u16 = env::var("HTTP_PORT")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(8080);
    let ttl_minutes: u64 = env::var("TTL_MINUTES")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(60);
    let cleanup_interval: u64 = env::var("CLEANUP_INTERVAL_MINUTES")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(5);
    let http_poll_interval: u64 = env::var("HTTP_POLL_INTERVAL")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(1);
    let http_failure_threshold: u32 = env::var("HTTP_FAILURE_THRESHOLD")
        .ok().and_then(|v| v.parse().ok()).unwrap_or(5);
    let sequencer_address = env::var("SEQUENCER_ADDRESS").ok();
    let mode = env::var("MODE").unwrap_or_else(|_| "gossip".to_string());

    fs::create_dir_all(&output_dir)?;

    info!("🚀 kona-preconf-service starting");
    info!("⛓️  Chain ID: {}", chain_id);
    info!("📁 Output:   {:?}", output_dir);
    info!("🌐 HTTP:     0.0.0.0:{}", http_port);

    let stats = Arc::new(Mutex::new(KonaBridgeStats {
        connected_peers: 0, received_preconfs: 0, processed_preconfs: 0, failed_preconfs: 0,
        http_received: 0, http_processed: 0, gossip_received: 0, gossip_processed: 0,
        mode_switches: 0, current_mode: 0, total_gaps: 0, http_gaps: 0,
        gossip_gaps: 0, bitmask_gaps: 0,
    }));
    let running = Arc::new(Mutex::new(true));
    let (sse_tx, _) = broadcast::channel::<u64>(64);

    // HTTP server runs in background
    {
        let srv_dir   = output_dir.clone();
        let srv_stats = stats.clone();
        let srv_sse   = sse_tx.clone();
        tokio::spawn(async move {
            if let Err(e) = server::run_http_server(srv_dir, chain_id, http_port, srv_stats, srv_sse).await {
                tracing::error!("HTTP server error: {}", e);
            }
        });
    }

    // P2P / HTTP-first network (blocks until shutdown)
    let network_name = match chain_id {
        10      => "op-mainnet",
        8453    => "base",
        130     => "unichain",
        480     => "worldchain",
        7777777 => "zora",
        _       => { tracing::warn!("Unknown chain {}, using base config", chain_id); "base" }
    };

    let mut chain_config = ChainConfig::from(network_name, chain_id);
    if let Some(ref addr) = sequencer_address {
        if let Ok(parsed) = addr.parse::<Address>() {
            chain_config.unsafe_signer = parsed;
        }
    }

    // Start TTL cleanup
    {
        let cdir = output_dir.clone();
        let crun = running.clone();
        tokio::spawn(async move {
            cleanup_old_files(cdir, ttl_minutes, cleanup_interval, crun).await;
        });
    }

    let http_first = mode == "http-first" || mode == "http";
    if http_first {
        if let Some(http_endpoint) = chain_config.get_http_endpoint() {
            info!("🌐 MODE={} — HTTP primary ({})", mode, http_endpoint);
            let health_tracker = Arc::new(Mutex::new(HttpHealthTracker {
                consecutive_failures: 0,
                last_success: None,
                failure_threshold: http_failure_threshold,
                current_mode: BridgeMode::HttpOnly,
                consecutive_success_blocks: 0,
            }));
            run_http_primary_with_gossip_fallback(
                http_endpoint, chain_id, disc_port, gossip_port,
                &output_dir, http_poll_interval, &chain_config,
                sequencer_address.as_deref(), health_tracker, stats, running,
                Arc::new(Mutex::new(BlockDeduplicator::new())),
                Arc::new(Mutex::new(BlockBitmaskTracker::new())),
                Some(sse_tx),
            ).await.map_err(|e| anyhow::anyhow!("{}", e))?;
        } else {
            warn!("MODE={} but no HTTP endpoint — starting gossip", mode);
            gossip::run_gossip_network(
                chain_id, disc_port, gossip_port, &output_dir,
                &chain_config, sequencer_address.as_deref(),
                stats, running, None, None, Some(sse_tx),
            ).await.map_err(|e| anyhow::anyhow!("{}", e))?;
        }
    } else {
        info!("📡 MODE=gossip — P2P only (set MODE=http-first to poll HTTP)");
        gossip::run_gossip_network(
            chain_id, disc_port, gossip_port, &output_dir,
            &chain_config, sequencer_address.as_deref(),
            stats, running, None, None, Some(sse_tx),
        ).await.map_err(|e| anyhow::anyhow!("{}", e))?;
    }

    Ok(())
}
