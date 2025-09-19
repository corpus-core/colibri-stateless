// http.rs - HTTP-Polling Logik für Preconfs

use crate::{
    config::ChainConfig,
    gossip,
    types::{BridgeMode, BlockBitmaskTracker, BlockDeduplicator, HttpHealthTracker, KonaBridgeStats},
    utils::{extract_block_number_from_preconf_data, extract_block_hash_from_preconf_data, update_symlinks_lib},
};
use reqwest;
use std::{
    path::PathBuf,
    sync::{Arc, Mutex},
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{
    fs as tokio_fs, 
    task::JoinHandle,
    time::{interval, sleep}
};
use tracing::{info, warn};

/// Robust hex decoding that handles odd-length strings
fn safe_hex_decode(hex_str: &str) -> Result<Vec<u8>, hex::FromHexError> {
    let clean_hex = hex_str.trim_start_matches("0x");
    let padded_hex = if clean_hex.len() % 2 == 1 {
        format!("0{}", clean_hex)
    } else {
        clean_hex.to_string()
    };
    hex::decode(&padded_hex)
}

/// Aggressive Polling-Strategie bei erkannten Block-Lücken
async fn try_fill_block_gaps(
    client: &reqwest::Client,
    endpoint: &str,
    last_block: u64,
    current_block: u64,
    chain_id: u64,
    output_dir: &PathBuf,
    max_attempts: u32,
) -> u32 {
    let gap = current_block.saturating_sub(last_block).saturating_sub(1);
    if gap == 0 {
        return 0;
    }
    
    info!("🔍 Attempting to fill {} block gaps between {} and {}", gap, last_block, current_block);
    
    let mut filled_blocks = 0;
    
    // Try aggressive polling for a few seconds to catch missed blocks
    for attempt in 1..=max_attempts {
        if let Ok(Some((_, preconf_data))) = fetch_http_preconf(client, endpoint).await {
            if let Ok(block_num) = process_http_preconf(preconf_data, chain_id, output_dir).await {
                if block_num > last_block && block_num < current_block {
                    filled_blocks += 1;
                    info!("📦 Gap-fill: Found missing block {}", block_num);
                }
            }
        }
        
        // Short delay between attempts
        sleep(Duration::from_millis(100)).await;
        
        if attempt % 10 == 0 {
            info!("🔍 Gap-fill attempt {}/{} (found {} blocks)", attempt, max_attempts, filled_blocks);
        }
    }
    
    if filled_blocks > 0 {
        info!("✅ Gap-fill completed: recovered {} of {} missing blocks", filled_blocks, gap);
    } else {
        warn!("⚠️  Gap-fill failed: could not recover any of {} missing blocks", gap);
    }
    
    filled_blocks
}

/// Fetch preconf from HTTP endpoint (similar to Go implementation)
pub async fn fetch_http_preconf(
    client: &reqwest::Client,
    endpoint: &str,
) -> Result<Option<(String, serde_json::Value)>, Box<dyn std::error::Error + Send + Sync>> {
    let response = client
        .get(endpoint)
        .timeout(Duration::from_secs(2)) // Aggressive timeout for 2s block times
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
pub async fn process_http_preconf(
    preconf: serde_json::Value,
    chain_id: u64,
    output_dir: &PathBuf,
) -> Result<u64, Box<dyn std::error::Error + Send + Sync>> {
    // Extract data and signature from JSON
    let data_hex = preconf["data"]
        .as_str()
        .ok_or("Missing 'data' field")?;
    let signature = &preconf["signature"];
    
    // Decode hex data (fix odd-length hex strings)
    let data_bytes = safe_hex_decode(data_hex)
        .map_err(|e| format!("Failed to decode data field '{}': {}", data_hex, e))?;
    
    // Create signature bytes (r + s + v)
    let r_hex = signature["r"].as_str().ok_or("Missing signature.r")?;
    let s_hex = signature["s"].as_str().ok_or("Missing signature.s")?;
    let y_parity = signature["yParity"].as_str().unwrap_or("0x0");
    
    // Fix odd-length hex strings for signature components
    let r_bytes = safe_hex_decode(r_hex)
        .map_err(|e| format!("Failed to decode signature.r '{}': {}", r_hex, e))?;
    let s_bytes = safe_hex_decode(s_hex)
        .map_err(|e| format!("Failed to decode signature.s '{}': {}", s_hex, e))?;
    let v_byte = if y_parity == "0x1" { 28 } else { 27 };
    
    // Pad r and s to 32 bytes and create signature
    let mut sig_bytes = [0u8; 65];
    sig_bytes[32 - r_bytes.len()..32].copy_from_slice(&r_bytes);
    sig_bytes[64 - s_bytes.len()..64].copy_from_slice(&s_bytes);
    sig_bytes[64] = v_byte;
    
    // Extract block number from raw data using fixed offsets
    let block_number = extract_block_number_from_preconf_data(&data_bytes)?;
//    info!("🔍 HTTP: Extracted block number {} from preconf data", block_number);
    
    // Compress HTTP preconfs with ZSTD (level 1 for speed)
    let compressed = zstd::bulk::compress(&data_bytes, 1)?;
    let compressed_size = compressed.len(); // Speichere Größe vor dem Move
    
    // Combine compressed payload + signature
    let mut combined = compressed;
    combined.extend_from_slice(&sig_bytes);
    
    // Save to filesystem
    let filename = format!("block_{}_{}.raw", chain_id, block_number);
    let filepath = output_dir.join(&filename);
    tokio_fs::write(&filepath, &combined).await?;
    
    // Update symlinks (latest.raw and pre_latest.raw) 
    update_symlinks_lib(output_dir, &filename, chain_id).await?;
    
    // Create metadata JSON with extracted block hash
    let block_hash = extract_block_hash_from_preconf_data(&data_bytes)
        .unwrap_or([0u8; 32]); // Fallback bei Fehlern
    
    let meta_filename = format!("block_{}_{}.json", chain_id, block_number);
    let meta_filepath = output_dir.join(&meta_filename);
    
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
    let metadata = serde_json::json!({
        "chain_id": chain_id.to_string(),
        "block_number": block_number,
        "block_hash": format!("0x{}", hex::encode(&block_hash)),
        "received_unix": timestamp,
        "signature": format!("0x{}", hex::encode(&sig_bytes)),
        "compressed_size": compressed_size,
        "decompressed_size": data_bytes.len(),
        "file_path": filename,
        "source": "http",
        "kona_p2p": true
    });

    let metadata_json = serde_json::to_string_pretty(&metadata)
        .map_err(|e| format!("Failed to serialize metadata: {}", e))?;
    
    tokio_fs::write(&meta_filepath, metadata_json).await
        .map_err(|e| format!("Failed to write metadata: {}", e))?;
    
    Ok(block_number)
}

/// HTTP-Primary mit Gossip-Fallback
pub async fn run_http_primary_with_gossip_fallback(
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
    deduplicator: Arc<Mutex<BlockDeduplicator>>,
    bitmask_tracker: Arc<Mutex<BlockBitmaskTracker>>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    
    let client = reqwest::Client::new();
    let mut interval_timer = interval(Duration::from_secs(http_poll_interval));
    let mut last_data_hash: Option<String> = None;
    let mut last_block_number: Option<u64> = None;
    let mut consecutive_same_data = 0u32;
    let mut last_status_block = 0u64;
    // Removed unused variable last_http_processed
    let mut gaps_since_last_status = 0u32;
    const STATUS_INTERVAL: u64 = 200; // Status alle 200 Blöcke (ca. 6 Minuten)
    
    // Gossip-Task-Verwaltung für Hybrid-Modus
    let mut gossip_task: Option<JoinHandle<()>> = None;
    
    info!("🌐 HTTP-primary mode active: {} ({}s interval)", http_endpoint, http_poll_interval);
    
    while *running.lock().unwrap() {
        interval_timer.tick().await;
        
        if !*running.lock().unwrap() {
            break;
        }
        
        // Check if we should switch to gossip mode (failure threshold)
        {
            let tracker = health_tracker.lock().unwrap();
            if tracker.current_mode == BridgeMode::GossipFallback {
                info!("🌐➡️📡 SWITCHING: HTTP → P2P Gossip (failure threshold reached)");
                drop(tracker);
                break;
            }
        }
        
        // Simplified: Only check for HTTP failures, not time-based switching
        
        // Try HTTP polling
        match fetch_http_preconf(&client, &http_endpoint).await {
            Ok(Some((data_hash, preconf_data))) => {
                // Check if this is new data
                if let Some(ref last_hash) = last_data_hash {
                    if *last_hash == data_hash {
                        consecutive_same_data += 1;
                        
                        // If we've seen the same data multiple times, poll more aggressively
                        let wait_time = if consecutive_same_data > 3 {
                            Duration::from_millis(200) // Very aggressive polling after 3 identical responses
                        } else {
                            Duration::from_millis(500)
                        };
                        
                        sleep(wait_time).await;
                        continue;
                    }
                }
                
                consecutive_same_data = 0; // Reset counter on new data
                last_data_hash = Some(data_hash.clone());
//                info!("🌐 HTTP: New preconf received (hash: {})", &data_hash[..16]);
                
                // Reset failure counter on success
                {
                    let mut tracker = health_tracker.lock().unwrap();
                    tracker.consecutive_failures = 0;
                    tracker.last_success = Some(SystemTime::now());
                }
                
                // Process and save the preconf
                    match process_http_preconf(preconf_data, chain_id, output_dir).await {
                        Ok(block_number) => {
                            // FIXED: Only check for duplicates in hybrid mode (when gossip is active)
                            let is_duplicate = {
                                let tracker = health_tracker.lock().unwrap();
                                if tracker.current_mode == BridgeMode::HttpPlusGossip {
                                    // Only check duplicates when gossip is running parallel
                                    let dedup = deduplicator.lock().unwrap();
                                    dedup.is_duplicate(block_number)
                                } else {
                                    // In HTTP-only mode, never treat as duplicate
                                    false
                                }
                            };
                            
                            if is_duplicate {
                                tracing::debug!("🛡️  HTTP: Block {} already processed by Gossip - skipping duplicate", block_number);
                                
                                // For duplicates: Only count as HTTP received, not processed
                                {
                                    let mut stats_guard = stats.lock().unwrap();
                                    stats_guard.http_received += 1;
                                    // Don't increment processed counters for duplicates
                                }
                                
                                continue; // Skip processing completely
                            }
                        // Check for gaps in block numbers
                        if let Some(last_num) = last_block_number {
                            let gap = block_number.saturating_sub(last_num);
                            if gap > 1 {
                                let missing_blocks = gap - 1;
                                warn!("🔍 HTTP: Block gap detected! {} -> {} (missing {} blocks)",
                                      last_num, block_number, missing_blocks);
                                
                                // Zähle Gaps für Status-Meldung
                                gaps_since_last_status += missing_blocks as u32;
                                
                                // Update gap statistics 
                                {
                                    let mut stats_guard = stats.lock().unwrap();
                                    stats_guard.http_gaps += missing_blocks as u32;
                                }
                                
                                // SIMPLIFIED: Only switch to gossip for LARGE gaps (>= 50 blocks)
                                let should_switch_to_gossip = {
                                    let mut tracker = health_tracker.lock().unwrap();
                                    if missing_blocks >= 50 && tracker.current_mode == BridgeMode::HttpOnly {
                                        warn!("🔍 HTTP: LARGE gap detected! {} blocks missing (threshold: 50 blocks)", missing_blocks);
                                        warn!("🌐+📡 STARTING: Gossip backup (HTTP gap too large)");
                                        
                                        tracker.current_mode = BridgeMode::HttpPlusGossip;
                                        tracker.consecutive_success_blocks = 0;
                                        true
                                    } else {
                                        false
                                    }
                                };
                                
                                if should_switch_to_gossip {
                                    // Update mode switch statistics
                                    {
                                        let mut stats_guard = stats.lock().unwrap();
                                        stats_guard.mode_switches += 1;
                                        stats_guard.current_mode = 2; // Hybrid mode (HTTP + Gossip)
                                    }
                                    
                                    // Start Gossip-Backup parallel zu HTTP
                                    if gossip_task.is_none() {
                                        warn!("🚀 Starting parallel Gossip backup task - CPU load will increase to ~127%");
                                        let gossip_chain_config = chain_config.clone();
                                        let gossip_output_dir = output_dir.clone();
                                        let gossip_stats = stats.clone();
                                        let gossip_running = running.clone();
                                        let gossip_sequencer = expected_sequencer.map(|s| s.to_string());
                                        let gossip_deduplicator = deduplicator.clone();
                                        let gossip_bitmask_tracker = bitmask_tracker.clone(); // Clone for async move
                                        
                                        gossip_task = Some(tokio::spawn(async move {
                                            info!("📡 Gossip backup task started");
                                            if let Err(e) = gossip::run_gossip_network(
                                                chain_id,
                                                disc_port,
                                                gossip_port,
                                                &gossip_output_dir,
                                                &gossip_chain_config,
                                                gossip_sequencer.as_deref(),
                                                gossip_stats,
                                                gossip_running,
                                                Some(gossip_deduplicator),
                                                Some(gossip_bitmask_tracker), // CRITICAL FIX: Pass bitmask_tracker to gossip
                                            ).await {
                                                warn!("📡 Gossip backup failed: {}", e);
                                            }
                                            info!("📡 Gossip backup task stopped");
                                        }));
                                        
                                        info!("🔧 HTTP continues polling while gossip backup is active");
                                    }
                                }
                                
                                // Try to fill gaps with aggressive polling (only if not switching)
                                let filled = try_fill_block_gaps(
                                    &client,
                                    &http_endpoint,
                                    last_num,
                                    block_number,
                                    chain_id,
                                    output_dir,
                                    20 // max 20 attempts = 2 seconds of aggressive polling
                                ).await;
                                
                                if filled > 0 {
                                    info!("🔧 Gap-fill recovered {}/{} missing blocks", filled, missing_blocks);
                                } else {
                                    warn!("⚠️  Gap-fill failed: HTTP endpoint reliability issue detected");
                                }
                            }
                        } else {
                            // No gap - increment success counter for gossip stopping
                            let should_stop_gossip = {
                                let mut tracker = health_tracker.lock().unwrap();
                                tracker.consecutive_success_blocks += 1;
                                
                                // SIMPLIFIED: Stop gossip after 50 consecutive successful HTTP blocks
                                if tracker.current_mode == BridgeMode::HttpPlusGossip && 
                                   tracker.consecutive_success_blocks >= 50 {
                                    warn!("🌐-📡 STOPPING: Gossip backup ({} consecutive successful HTTP blocks)", 
                                          tracker.consecutive_success_blocks);
                                    tracker.current_mode = BridgeMode::HttpOnly;
                                    tracker.consecutive_success_blocks = 0;
                                    true
                                } else {
                                    false
                                }
                            };
                            
                            if should_stop_gossip {
                                // Update mode statistics
                                let mut stats_guard = stats.lock().unwrap();
                                stats_guard.current_mode = 0; // HTTP only
                                drop(stats_guard);
                                
                                // Stop Gossip-Backup Task
                                if let Some(task) = gossip_task.take() {
                                    warn!("🛑 Stopping gossip backup task - HTTP stable again");
                                    task.abort();
                                    warn!("✅ HTTP recovered - gossip backup stopped");
                                }
                            }
                        }
                        last_block_number = Some(block_number);
                        
                        // Update bitmask tracker with processed block
                        {
                            let mut tracker = bitmask_tracker.lock().unwrap();
                            tracker.mark_block_processed(block_number);
                        }
                        
                        // Mark block as processed in deduplicator (for hybrid mode)
                        {
                            let mut dedup = deduplicator.lock().unwrap();
                            dedup.mark_processed(block_number);
                        }
                        
                        // Update success tracking (simplified)
                        {
                            let mut tracker = health_tracker.lock().unwrap();
                            tracker.last_success = Some(SystemTime::now());
                        }
                        
                        // Update stats
                        {
                            let mut stats_guard = stats.lock().unwrap();
                            stats_guard.received_preconfs += 1;
                            stats_guard.processed_preconfs += 1;
                            stats_guard.http_received += 1;
                            stats_guard.http_processed += 1;
                            
                            // Periodische Statusmeldung alle 200 Blöcke
                        if block_number >= last_status_block + STATUS_INTERVAL {
                            // SIMPLIFIED: Show simple success rate
                            let http_success_rate = if stats_guard.http_received > 0 { 
                                stats_guard.http_processed as f64 / stats_guard.http_received as f64 
                            } else { 
                                0.0 
                            };
                            
                            info!("🌐 HTTP Status: Block #{}, Total processed: {}, HTTP: {}/{} (rate: {:.3}), Gossip: {}, Gaps: {}", 
                                  block_number, stats_guard.processed_preconfs, 
                                  stats_guard.http_processed, stats_guard.http_received, http_success_rate, 
                                  stats_guard.gossip_processed, gaps_since_last_status);
                            last_status_block = block_number;
                            gaps_since_last_status = 0; // Reset gap counter
                        }
                        }
//                        info!("📡 HTTP: Processed preconf for block {} (source: HTTP polling)", block_number);
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
                // No new data, but HTTP is working - reset failure counter
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
            return crate::gossip::run_gossip_network(
                chain_id,
                disc_port,
                gossip_port,
                output_dir,
                chain_config,
                expected_sequencer,
                stats,
                running,
                Some(deduplicator), // Shared Deduplicator für Fallback
                Some(bitmask_tracker), // CRITICAL FIX: Pass bitmask_tracker to gossip fallback
            ).await;
        }
    }
    
    // Cleanup: Stoppe Gossip-Task falls noch aktiv
    if let Some(task) = gossip_task.take() {
        info!("🧹 Cleanup: Stopping remaining gossip backup task...");
        task.abort();
    }
    
    info!("🌐 HTTP-primary mode stopped");
    Ok(())
}
