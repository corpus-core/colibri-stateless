// processing.rs - Preconf-Verarbeitung und Dateisystem-Operations

use crate::utils::{signature_to_bytes, update_symlinks_lib};
use op_alloy_rpc_types_engine::{OpNetworkPayloadEnvelope, OpExecutionPayload};
use std::{
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};
use tokio::fs as tokio_fs;
use tracing::{debug};

/// Verarbeite Preconf mit korrektem Format (für Gossip-Netzwerk)
pub async fn process_preconf_with_correct_format(
    payload_envelope: &OpNetworkPayloadEnvelope,
    chain_id: u64,
    output_dir: &PathBuf,
    _expected_sequencer: Option<&str>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let block_number = payload_envelope.payload.block_number();
    let block_hash = payload_envelope.payload.block_hash();
    
//    info!("📦 Processing preconf for block #{}", block_number);

    // Verwende direkte Serialization ohne SSZ-Dependencies
    // Das Payload wird als JSON serialisiert und dann als Bytes behandelt
    let execution_payload_bytes = match &payload_envelope.payload {
        OpExecutionPayload::V1(payload) => serde_json::to_vec(payload).map_err(|e| format!("JSON serialization failed: {}", e))?,
        OpExecutionPayload::V2(payload) => serde_json::to_vec(payload).map_err(|e| format!("JSON serialization failed: {}", e))?,
        OpExecutionPayload::V3(payload) => serde_json::to_vec(payload).map_err(|e| format!("JSON serialization failed: {}", e))?,
        OpExecutionPayload::V4(payload) => serde_json::to_vec(payload).map_err(|e| format!("JSON serialization failed: {}", e))?,
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
    
//    info!("📊 Preconf data: {} bytes (domain: 32, payload: {})", 
//          preconf_data.len(), execution_payload_bytes.len());

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

//    info!("💾 Saved preconf to: {:?} ({} bytes)", filepath, final_data.len());

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

//    info!("📡 GOSSIP: Processed preconf for block {} (source: Gossip network)", block_number);
    
    Ok(())
}
