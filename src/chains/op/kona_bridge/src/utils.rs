// utils.rs - Hilfsfunktionen für die Kona-Bridge

use std::{
    path::PathBuf,
    sync::{Arc, Mutex},
    time::{Duration, SystemTime},
};
use tokio::{fs as tokio_fs, time::interval};
use tracing::{info, warn};

/// Extract block number from raw preconf data using fixed offsets
/// Struktur: 32 bytes previousBlockHash + ExecutionPayload
/// ExecutionPayload Offsets (wie in beacon_denep.c):
/// - parentHash: 32 bytes
/// - feeRecipient: 20 bytes  
/// - stateRoot: 32 bytes
/// - receiptsRoot: 32 bytes
/// - logsBloom: 256 bytes
/// - prevRandao: 32 bytes
/// - blockNumber: 8 bytes (little-endian) <- das wollen wir
pub fn extract_block_number_from_preconf_data(data: &[u8]) -> Result<u64, Box<dyn std::error::Error + Send + Sync>> {
    // Berechne Offset für blockNumber
    let previous_block_hash_offset = 32;
    let parent_hash_offset = 32;
    let fee_recipient_offset = 20;
    let state_root_offset = 32;
    let receipts_root_offset = 32;
    let logs_bloom_offset = 256;
    let prev_randao_offset = 32;
    
    let block_number_offset = previous_block_hash_offset 
        + parent_hash_offset 
        + fee_recipient_offset 
        + state_root_offset 
        + receipts_root_offset 
        + logs_bloom_offset 
        + prev_randao_offset;
    
    if data.len() < block_number_offset + 8 {
        return Err("Data too short to contain block number".into());
    }
    
    // Extrahiere 8 Bytes als little-endian u64
    let block_number_bytes = &data[block_number_offset..block_number_offset + 8];
    let block_number = u64::from_le_bytes([
        block_number_bytes[0], block_number_bytes[1], block_number_bytes[2], block_number_bytes[3],
        block_number_bytes[4], block_number_bytes[5], block_number_bytes[6], block_number_bytes[7],
    ]);
    
    Ok(block_number)
}

/// Extract block hash from raw preconf data using fixed offsets  
/// Block hash ist nach blockNumber + gasLimit + gasUsed + timestamp + extraData(4) + baseFeePerGas
pub fn extract_block_hash_from_preconf_data(data: &[u8]) -> Result<[u8; 32], Box<dyn std::error::Error + Send + Sync>> {
    // Berechne Offset für blockHash (nach blockNumber)
    let block_number_offset = 32 + 32 + 20 + 32 + 32 + 256 + 32; // wie oben berechnet
    let gas_limit_offset = 8;
    let gas_used_offset = 8;  
    let timestamp_offset = 8;
    let extra_data_offset = 4; // extraData ist dynamisch, aber meist 4 bytes
    let base_fee_offset = 32;
    
    let block_hash_offset = block_number_offset 
        + gas_limit_offset 
        + gas_used_offset 
        + timestamp_offset 
        + extra_data_offset 
        + base_fee_offset;
    
    if data.len() < block_hash_offset + 32 {
        return Err("Data too short to contain block hash".into());
    }
    
    // Extrahiere 32 Bytes für block hash
    let mut block_hash = [0u8; 32];
    block_hash.copy_from_slice(&data[block_hash_offset..block_hash_offset + 32]);
    
    Ok(block_hash)
}

/// Update both latest.raw and pre_latest.raw symlinks
pub async fn update_symlinks_lib(
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
        
//        info!("🔗 Updated pre_latest.raw symlink to {}", prev_filename);
    }
    
    // Update latest.raw
    let _ = tokio_fs::remove_file(&latest_path).await; // Ignore error if doesn't exist
    tokio_fs::symlink(new_latest_filename, &latest_path).await
        .map_err(|e| format!("Failed to create latest.raw symlink: {}", e))?;
    
//    info!("🔗 Updated latest.raw symlink to {}", new_latest_filename);
    
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
    
//    info!("🔍 Found previous block file: {} (from {} real block files)", previous_file, block_files.len());
    Ok(Some(previous_file.clone()))
}

/// Cleanup-Funktion für TTL-basierte Löschung alter Preconf-Dateien
pub async fn cleanup_old_files(
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
            Ok(_deleted_count) => {
                // Cleanup-Meldung wird bereits in cleanup_expired_files() geloggt
                // Note: Status wird bereits in cleanup_expired_files() geloggt
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
    let mut deleted_files = Vec::new();
    
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
                                    // Sammle gelöschte Dateien für Zusammenfassung
                                    let filename = path.file_name()
                                        .unwrap_or_default()
                                        .to_string_lossy()
                                        .to_string();
                                    deleted_files.push((filename, age.as_secs() / 60));
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
    
    // Zusammenfassung der gelöschten Dateien loggen
    if !deleted_files.is_empty() {
        // Zeige ersten paar Dateien und Zusammenfassung
        if deleted_files.len() <= 3 {
            // Wenige Dateien - zeige alle
            let file_list: Vec<String> = deleted_files.iter()
                .map(|(name, age)| format!("{} ({}min)", name, age))
                .collect();
            info!("🗑️  Deleted {} expired files: {}", deleted_count, file_list.join(", "));
        } else {
            // Viele Dateien - zeige nur Zusammenfassung
            let oldest_age = deleted_files.iter().map(|(_, age)| *age).max().unwrap_or(0);
            let newest_age = deleted_files.iter().map(|(_, age)| *age).min().unwrap_or(0);
            info!("🗑️  Deleted {} expired files (age: {}min - {}min)", 
                  deleted_count, newest_age, oldest_age);
        }
    }
    
    Ok(deleted_count)
}

/// Konvertiere Alloy-Signatur zu 65-Byte-Array
pub fn signature_to_bytes(signature: &alloy::signers::Signature) -> [u8; 65] {
    let mut bytes = [0u8; 65];
    bytes[..32].copy_from_slice(&signature.r().to_be_bytes::<32>());
    bytes[32..64].copy_from_slice(&signature.s().to_be_bytes::<32>());
    // Convert recovery ID (0/1) to Ethereum v parameter (27/28)
    bytes[64] = if signature.v() { 28 } else { 27 };
    bytes
}
