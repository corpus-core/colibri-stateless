// gossip.rs - Signature-preserving Optimism gossip loop on the last op-rs/kona commit.

use crate::{
    config::ChainConfig,
    processing::process_preconf_with_correct_format,
    types::{BlockBitmaskTracker, BlockDeduplicator, KonaBridgeStats},
};
use discv5::{ConfigBuilder, ListenConfig};
use k256::ecdsa::SigningKey;
use kona_disc::{Discv5Builder, LocalNode};
use kona_gossip::{
    default_config_builder, ConnectionGater, Event as GossipEvent, GaterConfig, GossipDriverBuilder,
};
use kona_peers::{enr_to_multiaddr, BootNode, BootNodes};
use kona_registry::ROLLUP_CONFIGS;
use libp2p::{
    core::ConnectedPoint,
    gossipsub, identify,
    identity::{secp256k1, Keypair},
    multiaddr::Protocol,
    swarm::{ConnectionId, SwarmEvent},
    Multiaddr, PeerId,
};
use std::{
    collections::{HashMap, HashSet},
    fs,
    io::{Read, Write},
    net::{IpAddr, Ipv4Addr, UdpSocket},
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
use tokio::sync::broadcast;
use tracing::{error, info, warn};

/// IP written into the local discv5 ENR.
///
/// `0.0.0.0` is useless for inbound dials. `ADVERTISE_IP` overrides; otherwise we
/// take the outbound interface address (still behind NAT, but at least routable
/// on the local network). Listen sockets stay on `0.0.0.0`.
fn advertise_ip() -> IpAddr {
    if let Ok(raw) = std::env::var("ADVERTISE_IP") {
        if let Ok(ip) = raw.parse() {
            return ip;
        }
        warn!(
            "⚠️  ADVERTISE_IP={:?} is not a valid IP, falling back to local interface",
            raw
        );
    }
    UdpSocket::bind("0.0.0.0:0")
        .and_then(|s| {
            s.connect("1.1.1.1:53")?;
            s.local_addr()
        })
        .map(|addr| addr.ip())
        .unwrap_or(IpAddr::V4(Ipv4Addr::UNSPECIFIED))
}

fn is_unroutable_advertise_ip(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => {
            v4.is_unspecified() || v4.is_loopback() || v4.is_private() || v4.is_link_local()
        }
        IpAddr::V6(v6) => v6.is_unspecified() || v6.is_loopback() || v6.is_unique_local(),
    }
}

/// File that holds the 32-byte secp256k1 secret (hex, no `0x`). Cleanup only
/// deletes `.raw`/`.json`, so this survives TTL sweeps.
fn p2p_identity_path(output_dir: &Path) -> PathBuf {
    output_dir.join("p2p.secp256k1")
}

fn keypair_from_secret_bytes(
    secret_bytes: [u8; 32],
) -> Result<(Keypair, SigningKey), Box<dyn std::error::Error + Send + Sync>> {
    let signing_key = SigningKey::from_slice(&secret_bytes)
        .map_err(|e| format!("invalid secp256k1 secret for discv5: {e}"))?;
    let mut sk_copy = secret_bytes;
    let secret = secp256k1::SecretKey::try_from_bytes(&mut sk_copy)
        .map_err(|e| format!("invalid secp256k1 secret for libp2p: {e}"))?;
    let keypair = Keypair::from(secp256k1::Keypair::from(secret));
    Ok((keypair, signing_key))
}

fn load_p2p_identity(
    path: &Path,
) -> Result<(Keypair, SigningKey), Box<dyn std::error::Error + Send + Sync>> {
    let meta = fs::symlink_metadata(path)
        .map_err(|e| format!("failed to stat {}: {e}", path.display()))?;
    if meta.file_type().is_symlink() || !meta.is_file() {
        return Err(format!("{} must be a regular file (not a symlink)", path.display()).into());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o600))
            .map_err(|e| format!("failed to chmod 0600 {}: {e}", path.display()))?;
    }
    let file =
        fs::File::open(path).map_err(|e| format!("failed to read {}: {e}", path.display()))?;
    let mut buf = Vec::new();
    file.take(80)
        .read_to_end(&mut buf)
        .map_err(|e| format!("failed to read {}: {e}", path.display()))?;
    if buf.len() > 65 {
        return Err(format!("{} is too large to be a 32-byte hex secret", path.display()).into());
    }
    let raw =
        std::str::from_utf8(&buf).map_err(|_| format!("{} is not valid UTF-8", path.display()))?;
    let decoded =
        hex::decode(raw.trim()).map_err(|e| format!("{} is not valid hex: {e}", path.display()))?;
    let secret_bytes: [u8; 32] = decoded.as_slice().try_into().map_err(|_| {
        format!(
            "{} must contain exactly 32 bytes (got {})",
            path.display(),
            decoded.len()
        )
    })?;
    keypair_from_secret_bytes(secret_bytes)
}

fn create_p2p_identity(
    path: &Path,
) -> Result<(Keypair, SigningKey), Box<dyn std::error::Error + Send + Sync>> {
    let generated = Keypair::generate_secp256k1();
    let secret_bytes = generated
        .try_into_secp256k1()
        .map_err(|e| format!("generated key is not secp256k1: {e}"))?
        .secret()
        .to_bytes();
    let mut opts = fs::OpenOptions::new();
    opts.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        opts.mode(0o600);
    }
    let mut file = opts
        .open(path)
        .map_err(|e| format!("failed to create {}: {e}", path.display()))?;
    file.write_all(hex::encode(secret_bytes).as_bytes())
        .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
    file.sync_all()
        .map_err(|e| format!("failed to fsync {}: {e}", path.display()))?;
    info!("🔑 Generated P2P identity at {}", path.display());
    keypair_from_secret_bytes(secret_bytes)
}

/// Load a stable secp256k1 identity, or create and persist one.
///
/// discv5 ENR peer id and libp2p Noise peer id **must** be the same key.
/// Generating two random keys made inbound dials fail with "Unexpected peer ID"
/// as soon as a remote compared our ENR against the Noise handshake.
fn load_or_create_p2p_identity(
    output_dir: &Path,
) -> Result<(Keypair, SigningKey), Box<dyn std::error::Error + Send + Sync>> {
    let path = p2p_identity_path(output_dir);
    if path.exists() {
        return load_p2p_identity(&path);
    }
    match create_p2p_identity(&path) {
        Ok(keys) => Ok(keys),
        Err(e) if path.exists() => load_p2p_identity(&path)
            .map_err(|load_err| format!("create failed ({e}); reload failed ({load_err})").into()),
        Err(e) => Err(e),
    }
}

fn endpoint_dir(endpoint: &ConnectedPoint) -> &'static str {
    match endpoint {
        ConnectedPoint::Dialer { .. } => "outbound",
        ConnectedPoint::Listener { .. } => "inbound",
    }
}

fn endpoint_addr(endpoint: &ConnectedPoint) -> String {
    match endpoint {
        ConnectedPoint::Dialer { address, .. } => address.to_string(),
        ConnectedPoint::Listener { send_back_addr, .. } => send_back_addr.to_string(),
    }
}

fn is_blocks_topic(handler: &kona_gossip::BlockHandler, topic: &gossipsub::TopicHash) -> bool {
    *topic == handler.blocks_v1_topic.hash()
        || *topic == handler.blocks_v2_topic.hash()
        || *topic == handler.blocks_v3_topic.hash()
        || *topic == handler.blocks_v4_topic.hash()
}

/// Enough topic peers to receive sequencer publishes. More peers only add
/// gossip traffic and libp2p/discv5 state — this process is a listener, not a relay.
const MESH_TARGET: usize = 3;
const MAX_IN_FLIGHT_DIALS: usize = 2;
/// Cap outbound discovery dials. Mesh target is much smaller; extras are fallbacks.
const MAX_OUTBOUND: usize = 8;
/// Cap inbound gossip connections. Without this, a public ENR accumulates
/// the whole Base mesh (hundreds of live TCP sessions, GB-scale RSS).
const MAX_INBOUND: usize = 8;
/// Hard cap on simultaneous gossip peers (inbound + outbound).
const MAX_TOTAL: usize = 16;
/// Drop gater dial history older than this so `dialed_peers` cannot grow without bound.
const GATER_DIAL_RETENTION: Duration = Duration::from_secs(10 * 60);

/// `inbound` / `total` are counts *after* inserting the new listener peer.
fn inbound_over_limit(inbound: usize, total: usize) -> bool {
    inbound > MAX_INBOUND || total > MAX_TOTAL
}

/// Only the first `MESH_TARGET` block-topic subscribers become explicit peers.
/// Explicit peers are never rotated out by gossipsub and would otherwise pin
/// every historical subscriber in the mesh.
fn can_promote_explicit(topic_peers: usize) -> bool {
    topic_peers <= MESH_TARGET
}

/// Inbound subscribers must not freeze discovery. We only stop dialing once
/// enough *outbound* peers sit on a blocks topic.
fn outbound_topic_count(topic_peers: &HashSet<PeerId>, outbound_peers: &HashSet<PeerId>) -> usize {
    topic_peers.intersection(outbound_peers).count()
}

fn forget_peer_state(gossip: &mut kona_gossip::GossipDriver<ConnectionGater>, peer_id: &PeerId) {
    gossip.peerstore.remove(peer_id);
    gossip.connection_gate.connectedness.remove(peer_id);
    gossip
        .swarm
        .behaviour_mut()
        .gossipsub
        .remove_explicit_peer(peer_id);
}

fn prune_stale_gater(gater: &mut ConnectionGater, max_age: Duration) {
    gater
        .dialed_peers
        .retain(|_, info| info.last_dial.elapsed() < max_age);
}

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

    // One secp256k1 key for both discv5 ENR and libp2p Noise. Persisted so a
    // restart keeps the same PeerId (remotes can still find us after a crash).
    let (gossip_keypair, disc_signing_key) = load_or_create_p2p_identity(output_dir)?;
    let gossip_pubkey = gossip_keypair.public();
    let local_peer_id = PeerId::from(gossip_pubkey.clone());

    // Listen on all interfaces; advertise the gossip TCP port (not the disc UDP port)
    // so peers that dial our ENR actually hit the libp2p swarm.
    let advertised = advertise_ip();
    let discovery_address = LocalNode::new(disc_signing_key, advertised, gossip_port, disc_port);
    let discovery_config = ConfigBuilder::new(ListenConfig::Ipv4 {
        ip: Ipv4Addr::UNSPECIFIED,
        port: disc_port,
    })
    // kona's bootstrap awaits request_enr() for every enode:// bootnode
    // sequentially. The discv5 default query timeout is 60s, so a cold
    // start can sit silent for minutes before gossip sees a single peer.
    .query_timeout(Duration::from_secs(2))
    .request_timeout(Duration::from_secs(1))
    .build();

    tracing::debug!(
        "🔍 Looking up rollup config for chain {}",
        chain_config.chain_id
    );
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
            format!(
                "No rollup config found for chain {} or Base fallback",
                chain_config.chain_id
            )
        })?
        .clone();
    tracing::debug!("✅ Found rollup config for chain {}", chain_config.chain_id);

    info!("🪪 P2P PeerId {}", local_peer_id);
    info!(
        "🔍 Discovery listen 0.0.0.0:{} (ENR advertises {} tcp={} udp={})",
        disc_port, advertised, gossip_port, disc_port
    );
    info!("📡 Gossip listen 0.0.0.0:{}", gossip_port);
    info!("🔐 Expected sequencer: {}", chain_config.unsafe_signer);
    if is_unroutable_advertise_ip(advertised) {
        warn!(
            "⚠️  Advertised IP {} is not publicly reachable. Other Base nodes cannot dial us, \
             so we only get outbound slots — and those are usually full (max ~30). \
             Set ADVERTISE_IP to the WAN address and forward UDP {} + TCP {}.",
            advertised, disc_port, gossip_port
        );
    }

    // Official OP-Stack mainnet/testnet bootnodes from kona-peers, filtered by
    // chain id on dial. Extra ENRs from ChainConfig (usually empty) are appended.
    let mut bootnodes = BootNodes::from_chain_id(chain_config.chain_id);
    for enr in &chain_config.bootnodes {
        bootnodes.0.push(BootNode::from(enr.clone()));
    }
    info!(
        "🔧 P2P bootstrap: {} bootnode(s) for chain {}",
        bootnodes.len(),
        chain_config.chain_id
    );

    let l2_chain_id = rollup_config.l2_chain_id.id();

    // Build the gossip driver (produces signed OpNetworkPayloadEnvelopes via handle_event).
    // The returned `unsafe_block_signer_sender` seeds a `watch::channel` inside the driver
    // with `chain_config.unsafe_signer` and is retained here on purpose: we do not rotate
    // the signer at runtime, and `watch` preserves the last value for readers even after
    // its sender is dropped. Binding it keeps the intent explicit.
    // One healthy topic peer is enough to receive publishes. Default kona mesh
    // wants Dlo=6, which kept us hunting and churning connections instead of
    // grafting the one Base node that actually spoke gossipsub.
    let mut gossip_cfg = default_config_builder();
    gossip_cfg.mesh_n(4).mesh_n_low(1).mesh_n_high(8);
    let gossip_cfg = gossip_cfg
        .build()
        .map_err(|e| format!("invalid gossipsub config: {e}"))?;
    let (mut gossip, _unsafe_block_signer_sender) = GossipDriverBuilder::new(
        rollup_config,
        chain_config.unsafe_signer,
        gossip_addr,
        gossip_keypair,
    )
    .with_config(gossip_cfg)
    .with_gater_config(GaterConfig {
        peer_redialing: Some(3),
        dial_period: Duration::from_secs(10 * 60),
    })
    .with_timeout(Duration::from_secs(5 * 60))
    .build()
    .map_err(|e| format!("Failed to build gossip driver: {}", e))?;

    // Match base-consensus identify (agent "base", empty protocol version).
    // Hide 0.0.0.0 listen addrs; advertise the ENR IP so peers get a usable address.
    gossip.behaviour_mut().identify = identify::Behaviour::new(
        identify::Config::new(String::new(), gossip_pubkey)
            .with_agent_version("base".to_string())
            .with_hide_listen_addrs(true),
    );
    if !advertised.is_unspecified() {
        let mut ext = Multiaddr::from(advertised);
        ext.push(Protocol::Tcp(gossip_port));
        gossip.swarm.add_external_address(ext);
    }

    // Start listening on the gossip multiaddr before wiring up discovery.
    gossip
        .start()
        .await
        .map_err(|e| format!("Failed to start gossip swarm: {}", e))?;

    // Official ENR bootnodes are discv5 bootstrap, not libp2p gossip peers.
    // After Base Azul (May 2026) TCP/9222 speaks Reth discv5; a Noise handshake
    // is RST'd immediately. Never gossip-dial those PeerIds.
    let mut discv5_boot_peers: HashSet<PeerId> = HashSet::new();
    for enr in bootnodes.iter().filter_map(|n| match n {
        BootNode::Enr(enr) => Some(enr),
        BootNode::Enode(_) => None,
    }) {
        if let Some(addr) = enr_to_multiaddr(enr) {
            if let Some(id) = ConnectionGater::peer_id_from_addr(&addr) {
                discv5_boot_peers.insert(id);
            }
        }
    }
    info!(
        "🔍 {} official ENR bootnode(s) reserved for discv5 (not gossip-dialed)",
        discv5_boot_peers.len()
    );

    // Build and spawn the discovery driver. `start()` returns a handle plus an ENR
    // receiver channel that the gossip loop dials into as peers show up. The handle owns
    // the request-mpsc sender that keeps the internal driver task alive; dropping it early
    // would cause kona's driver to hot-spin on the closed channel, so we bind it for the
    // full lifetime of this function.
    // Only signed ENRs go into kona's bootstrap list. `enode://` entries are
    // still added internally via `BootNodes::from_chain_id`, but if we pass them
    // here too they are requested twice — each `request_enr` can block the
    // discovery task for the query timeout.
    let discovery_bootnodes = BootNodes(
        bootnodes
            .iter()
            .filter(|n| matches!(n, BootNode::Enr(_)))
            .cloned()
            .collect(),
    );
    let discovery = Discv5Builder::new(discovery_address, l2_chain_id, discovery_config)
        .with_bootnodes(discovery_bootnodes)
        // Do not reuse ~/.kona/<id>/bootstore.json: a previous run with the
        // wrong (Unichain) bootnodes filled it with thousands of ENRs that
        // starve the gossip loop on startup.
        .with_bootstore_file(None)
        .with_interval(Duration::from_secs(10))
        .build()
        .map_err(|e| format!("Failed to build discovery driver: {}", e))?;
    let (_disc_handler, mut enr_receiver) = discovery.start();

    info!("📡 Gossip network started - listening for blocks...");

    let mut latest_block_number = 0u64;
    let mut last_status_block = 0u64;
    let mut next_peer_log = 0u64;
    const STATUS_INTERVAL: u64 = 200; // Status every ~200 blocks (~6 min on OP chains)
    const PEER_LOG_SECS: u64 = 30;

    // Poll the shutdown flag on a coarse interval so the select! branches wake up
    // regularly and can honour a stop request without busy-waiting.
    let mut shutdown_check = tokio::time::interval(Duration::from_secs(1));
    let started = std::time::Instant::now();
    let mut dial_cooldown: HashMap<PeerId, Instant> = HashMap::new();
    let mut conn_started: HashMap<PeerId, Instant> = HashMap::new();
    let mut outbound_peers: HashSet<PeerId> = HashSet::new();
    let mut inbound_peers: HashSet<PeerId> = HashSet::new();
    let mut topic_peers: HashSet<PeerId> = HashSet::new();
    const DIAL_COOLDOWN: Duration = Duration::from_secs(60);

    loop {
        tokio::select! {
            biased;
            event = gossip.next() => {
                let Some(event) = event else {
                    error!("🛑 GOSSIP: swarm stream ended");
                    break;
                };
                let mut promote_explicit: Option<PeerId> = None;
                let mut close_conn: Option<ConnectionId> = None;
                let mut saw_gossip_message = false;
                match &event {
                    SwarmEvent::ConnectionEstablished { peer_id, endpoint, connection_id, num_established, .. } => {
                        conn_started.insert(*peer_id, Instant::now());
                        if matches!(endpoint, ConnectedPoint::Dialer { .. }) {
                            outbound_peers.insert(*peer_id);
                            inbound_peers.remove(peer_id);
                        } else if outbound_peers.contains(peer_id) {
                            // Dual-connect: keep the outbound mesh session, drop the extra inbound.
                            close_conn = Some(*connection_id);
                        } else {
                            inbound_peers.insert(*peer_id);
                            if inbound_over_limit(inbound_peers.len(), gossip.connected_peers()) {
                                close_conn = Some(*connection_id);
                            }
                        }
                        info!(
                            "✅ Connected {} {} {} n={} (peers={} in={} out={})",
                            peer_id,
                            endpoint_dir(endpoint),
                            endpoint_addr(endpoint),
                            num_established,
                            gossip.connected_peers(),
                            inbound_peers.len(),
                            outbound_peers.len()
                        );
                    }
                    SwarmEvent::IncomingConnection { connection_id, send_back_addr, local_addr, .. } => {
                        tracing::debug!("⬅️  Incoming {} -> {}", send_back_addr, local_addr);
                        if inbound_peers.len() >= MAX_INBOUND || gossip.connected_peers() >= MAX_TOTAL {
                            close_conn = Some(*connection_id);
                        }
                    }
                    SwarmEvent::OutgoingConnectionError { peer_id, error, .. } => {
                        if let Some(id) = peer_id {
                            dial_cooldown.insert(*id, Instant::now());
                        }
                        warn!("❌ Dial failed peer={:?}: {}", peer_id, error);
                    }
                    SwarmEvent::ConnectionClosed { peer_id, endpoint, cause, num_established, .. } => {
                        let held = if *num_established == 0 {
                            conn_started
                                .remove(peer_id)
                                .map(|t| t.elapsed())
                                .unwrap_or_default()
                        } else {
                            Duration::ZERO
                        };
                        if *num_established == 0 {
                            outbound_peers.remove(peer_id);
                            inbound_peers.remove(peer_id);
                            topic_peers.remove(peer_id);
                            dial_cooldown.insert(*peer_id, Instant::now());
                            forget_peer_state(&mut gossip, peer_id);
                        } else if matches!(endpoint, ConnectedPoint::Listener { .. }) {
                            inbound_peers.remove(peer_id);
                        }
                        info!(
                            "🔌 Disconnected {} {} after {:?} cause={:?} remaining={} (peers={} in={} out={})",
                            peer_id,
                            endpoint_dir(endpoint),
                            held,
                            cause,
                            num_established,
                            gossip.connected_peers(),
                            inbound_peers.len(),
                            outbound_peers.len()
                        );
                    }
                    SwarmEvent::Behaviour(GossipEvent::Identify(ev)) => match ev.as_ref() {
                        identify::Event::Received { peer_id, info, .. } => {
                            info!(
                                "🆔 Identify from {}: agent={} proto={} protocols={:?} listen={:?}",
                                peer_id,
                                info.agent_version,
                                info.protocol_version,
                                info.protocols,
                                info.listen_addrs
                            );
                        }
                        identify::Event::Sent { peer_id, .. } => {
                            info!("🆔 Identify sent to {}", peer_id);
                        }
                        identify::Event::Error { peer_id, error, .. } => {
                            warn!("🆔 Identify error {}: {}", peer_id, error);
                        }
                        _ => {}
                    },
                    SwarmEvent::Behaviour(GossipEvent::Gossipsub(ev)) => match ev.as_ref() {
                        gossipsub::Event::GossipsubNotSupported { peer_id } => {
                            warn!("⚠️  Peer {} does not speak gossipsub", peer_id);
                        }
                        gossipsub::Event::Subscribed { peer_id, topic } => {
                            info!("📥 {} subscribed to {}", peer_id, topic);
                            if is_blocks_topic(&gossip.handler, topic) {
                                topic_peers.insert(*peer_id);
                                let outbound_topic =
                                    outbound_topic_count(&topic_peers, &outbound_peers);
                                if outbound_peers.contains(peer_id)
                                    && can_promote_explicit(outbound_topic)
                                {
                                    promote_explicit = Some(*peer_id);
                                }
                            }
                        }
                        gossipsub::Event::Unsubscribed { peer_id, topic } => {
                            info!("📤 {} unsubscribed from {}", peer_id, topic);
                            if is_blocks_topic(&gossip.handler, topic) {
                                topic_peers.remove(peer_id);
                                gossip
                                    .swarm
                                    .behaviour_mut()
                                    .gossipsub
                                    .remove_explicit_peer(peer_id);
                            }
                        }
                        gossipsub::Event::Message {
                            propagation_source,
                            message,
                            ..
                        } => {
                            saw_gossip_message = true;
                            info!(
                                "📨 Gossip message from {} topic={} len={}",
                                propagation_source,
                                message.topic,
                                message.data.len()
                            );
                        }
                        _ => {}
                    },
                    SwarmEvent::Behaviour(GossipEvent::Ping(ev)) => {
                        if let Err(e) = &ev.result {
                            warn!("🏓 Ping failed {}: {}", ev.peer, e);
                        }
                    }
                    _ => {}
                }
                if let Some(peer_id) = promote_explicit {
                    gossip
                        .swarm
                        .behaviour_mut()
                        .gossipsub
                        .add_explicit_peer(&peer_id);
                    info!("📌 Explicit mesh peer {}", peer_id);
                }
                let payload_envelope = gossip.handle_event(event);
                if let Some(connection_id) = close_conn {
                    info!(
                        "🚫 Closing connection {:?} (inbound {}/{} total {}/{})",
                        connection_id,
                        inbound_peers.len(),
                        MAX_INBOUND,
                        gossip.connected_peers(),
                        MAX_TOTAL
                    );
                    gossip.swarm.close_connection(connection_id);
                }
                let Some(payload_envelope) = payload_envelope else {
                    if saw_gossip_message {
                        warn!(
                            "⚠️  Gossip message dropped by kona handler (decode / signer / validation)"
                        );
                    }
                    continue;
                };

                let hash = payload_envelope.payload.block_hash();
                let number = payload_envelope.payload.block_number();
                info!("🎉 P2P: PRECONF RECEIVED! Block #{} Hash: {}", number, hash);

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
                        stats_guard.record_processed_preconf(number);

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
                if outbound_topic_count(&topic_peers, &outbound_peers) >= MESH_TARGET {
                    continue;
                }
                if outbound_peers.len() >= MAX_OUTBOUND || gossip.connected_peers() >= MAX_TOTAL {
                    continue;
                }
                if gossip.connection_gate.current_dials.len() >= MAX_IN_FLIGHT_DIALS {
                    continue;
                }
                let Some(addr) = enr_to_multiaddr(&enr) else {
                    continue;
                };
                let Some(peer_id) = ConnectionGater::peer_id_from_addr(&addr) else {
                    continue;
                };
                if discv5_boot_peers.contains(&peer_id) {
                    continue;
                }
                if gossip.swarm.is_connected(&peer_id) {
                    continue;
                }
                if gossip.connection_gate.current_dials.contains(&peer_id) {
                    continue;
                }
                if dial_cooldown.get(&peer_id).is_some_and(|t| t.elapsed() < DIAL_COOLDOWN) {
                    continue;
                }
                info!("📞 Dialing {} at {}", peer_id, addr);
                gossip.dial(enr);
            }
            _ = shutdown_check.tick() => {
                if !*running.lock().unwrap() {
                    break;
                }
                dial_cooldown.retain(|_, t| t.elapsed() < DIAL_COOLDOWN);
                prune_stale_gater(&mut gossip.connection_gate, GATER_DIAL_RETENTION);
                let connected: HashSet<PeerId> = gossip.swarm.connected_peers().cloned().collect();
                inbound_peers.retain(|id| connected.contains(id));
                outbound_peers.retain(|id| connected.contains(id));
                topic_peers.retain(|id| connected.contains(id));
                gossip.peerstore.retain(|id, _| connected.contains(id));
                let peers = gossip.connected_peers();
                {
                    let mut stats_guard = stats.lock().unwrap();
                    stats_guard.connected_peers = peers as u32;
                }
                let elapsed = started.elapsed().as_secs();
                if elapsed >= next_peer_log {
                    let v4 = gossip.handler.blocks_v4_topic.hash();
                    let mesh = gossip
                        .swarm
                        .behaviour()
                        .gossipsub
                        .mesh_peers(&v4)
                        .count();
                    info!(
                        "📡 GOSSIP peers: {} connected (in={} out={}/{}), topic={}, mesh_v4={}, peerstore={}, in-flight={}",
                        peers,
                        inbound_peers.len(),
                        outbound_peers.len(),
                        MAX_OUTBOUND,
                        topic_peers.len(),
                        mesh,
                        gossip.peerstore.len(),
                        gossip.connection_gate.current_dials.len()
                    );
                    next_peer_log = elapsed + PEER_LOG_SECS;
                }
            }
        }
    }

    info!("🛑 Gossip network stopping...");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn identity_is_stable_across_reload() {
        let dir = TempDir::new().unwrap();
        let (kp1, sk1) = load_or_create_p2p_identity(dir.path()).unwrap();
        let (kp2, sk2) = load_or_create_p2p_identity(dir.path()).unwrap();
        assert_eq!(PeerId::from(kp1.public()), PeerId::from(kp2.public()));
        assert_eq!(sk1.to_bytes(), sk2.to_bytes());
        assert!(p2p_identity_path(dir.path()).exists());
    }

    #[test]
    fn identity_rejects_symlink() {
        let dir = TempDir::new().unwrap();
        let target = dir.path().join("target");
        fs::write(&target, "00").unwrap();
        let link = p2p_identity_path(dir.path());
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink(&target, &link).unwrap();
            assert!(load_or_create_p2p_identity(dir.path()).is_err());
        }
    }

    #[cfg(unix)]
    #[test]
    fn identity_file_is_owner_rw_only() {
        use std::os::unix::fs::PermissionsExt;
        let dir = TempDir::new().unwrap();
        load_or_create_p2p_identity(dir.path()).unwrap();
        let mode = fs::metadata(p2p_identity_path(dir.path()))
            .unwrap()
            .permissions()
            .mode()
            & 0o777;
        assert_eq!(mode, 0o600);
    }

    #[test]
    fn inbound_ninth_peer_is_dropped() {
        assert!(!inbound_over_limit(MAX_INBOUND, MAX_INBOUND));
        assert!(inbound_over_limit(MAX_INBOUND + 1, MAX_INBOUND + 1));
    }

    #[test]
    fn inbound_is_dropped_when_total_cap_is_hit() {
        assert!(inbound_over_limit(1, MAX_TOTAL + 1));
        assert!(!inbound_over_limit(1, MAX_TOTAL));
    }

    #[test]
    fn only_mesh_target_subscribers_become_explicit() {
        assert!(can_promote_explicit(1));
        assert!(can_promote_explicit(MESH_TARGET));
        assert!(!can_promote_explicit(MESH_TARGET + 1));
    }

    #[test]
    fn inbound_subscribers_do_not_count_as_outbound_mesh() {
        let inbound: PeerId = PeerId::random();
        let outbound: PeerId = PeerId::random();
        let topic = HashSet::from([inbound, outbound]);
        let outbound_set = HashSet::from([outbound]);
        assert_eq!(outbound_topic_count(&topic, &outbound_set), 1);
    }
}
