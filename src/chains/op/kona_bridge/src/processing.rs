// processing.rs - Preconf capture: SSZ payload + zstd file layout for the C prover.

use crate::utils::{signature_to_bytes, update_symlinks_lib};
use op_alloy_rpc_types_engine::OpNetworkPayloadEnvelope;
use ssz::Encode;
use std::{
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};
use tokio::fs as tokio_fs;
use tracing::debug;

/// Uncompressed preconf bytes consumed by `op_el_from_preconf_bytes`:
/// `parentBeaconRoot (32) || SSZ(ExecutionPayload)`.
///
/// Isthmus/Electra `requestsHash` is omitted: the C parser defaults it to `sha256('')`.
/// The sequencer signature covers `keccak(zeros32 || chain_id32 || keccak(these bytes))`,
/// matching `PayloadHash::signature_message` for gossip v3/v4.
fn encode_prefixed_ssz_payload(envelope: &OpNetworkPayloadEnvelope) -> Vec<u8> {
    let mut out = Vec::new();
    match envelope.parent_beacon_block_root {
        Some(root) => out.extend_from_slice(root.as_slice()),
        None => out.extend_from_slice(&[0u8; 32]),
    }
    out.extend_from_slice(&envelope.payload.as_ssz_bytes());
    out
}

/// Write a gossip envelope as `zstd(prefix || SSZ payload) || signature(65)`.
pub async fn process_preconf_with_correct_format(
    payload_envelope: &OpNetworkPayloadEnvelope,
    chain_id: u64,
    output_dir: &PathBuf,
    _expected_sequencer: Option<&str>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let block_number = payload_envelope.payload.block_number();
    let block_hash = payload_envelope.payload.block_hash();

    let preconf_data = encode_prefixed_ssz_payload(payload_envelope);
    debug!(
        "🔍 SSZ preconf block #{} payload+prefix {} bytes",
        block_number,
        preconf_data.len()
    );

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

#[cfg(test)]
mod tests {
    use super::*;
    use alloy_primitives::{Signature, B256, U256};
    use op_alloy_rpc_types_engine::{OpExecutionPayload, OpNetworkPayloadEnvelope, PayloadHash};
    use ssz::Encode;

    /// Deneb (V3): extraData offset 528.
    const V3_JSON: &str = concat!(
        "{",
        r#"
        "parentHash":"0x0000000000000000000000000000000000000000000000000000000000000001",
        "feeRecipient":"0x0000000000000000000000000000000000000000",
        "stateRoot":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "receiptsRoot":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "logsBloom":"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
        "prevRandao":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "blockNumber":"0x1",
        "gasLimit":"0x1",
        "gasUsed":"0x0",
        "timestamp":"0x1",
        "extraData":"0x",
        "baseFeePerGas":"0x0",
        "blockHash":"0x0000000000000000000000000000000000000000000000000000000000000002",
        "transactions":[],
        "withdrawals":[],
        "blobGasUsed":"0x0",
        "excessBlobGas":"0x0"
    "#,
        "}"
    );

    /// Isthmus (V4): extraData offset 560 so C selects ISTHMUS_EXECUTION_PAYLOAD.
    const V4_JSON: &str = concat!(
        "{",
        r#"
        "parentHash":"0x0000000000000000000000000000000000000000000000000000000000000001",
        "feeRecipient":"0x0000000000000000000000000000000000000000",
        "stateRoot":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "receiptsRoot":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "logsBloom":"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
        "prevRandao":"0x0000000000000000000000000000000000000000000000000000000000000000",
        "blockNumber":"0x1",
        "gasLimit":"0x1",
        "gasUsed":"0x0",
        "timestamp":"0x1",
        "extraData":"0x",
        "baseFeePerGas":"0x0",
        "blockHash":"0x0000000000000000000000000000000000000000000000000000000000000002",
        "transactions":[],
        "withdrawals":[],
        "blobGasUsed":"0x0",
        "excessBlobGas":"0x0",
        "withdrawalsRoot":"0x0000000000000000000000000000000000000000000000000000000000000003"
    "#,
        "}"
    );

    fn envelope_from_json(json: &str, parent: Option<B256>) -> OpNetworkPayloadEnvelope {
        OpNetworkPayloadEnvelope {
            payload: serde_json::from_str::<OpExecutionPayload>(json).expect("payload json"),
            signature: Signature::new(U256::from(1), U256::from(1), false),
            payload_hash: PayloadHash::default(),
            parent_beacon_block_root: parent,
        }
    }

    fn extra_data_offset(prefixed: &[u8]) -> u32 {
        u32::from_le_bytes(prefixed[32 + 436..32 + 440].try_into().unwrap())
    }

    fn assert_parent_then_ssz(envelope: &OpNetworkPayloadEnvelope, parent: &[u8; 32], expected_offset: u32) {
        let bytes = encode_prefixed_ssz_payload(envelope);
        assert_eq!(&bytes[..32], parent);
        assert_ne!(bytes[32], b'{', "payload after prefix must be SSZ, not JSON");
        assert_eq!(
            bytes.len(),
            32 + envelope.payload.as_ssz_bytes().len(),
            "requestsHash must be omitted (C defaults to sha256(''))"
        );
        assert_eq!(extra_data_offset(&bytes), expected_offset);
    }

    #[test]
    fn prefixed_v4_ssz_is_parent_root_then_isthmus_payload() {
        let envelope = envelope_from_json(V4_JSON, Some(B256::repeat_byte(0xAB)));
        assert_parent_then_ssz(&envelope, &[0xABu8; 32], 560);
    }

    #[test]
    fn prefixed_v3_ssz_is_parent_root_then_deneb_payload() {
        let envelope = envelope_from_json(V3_JSON, Some(B256::repeat_byte(0xAB)));
        assert_parent_then_ssz(&envelope, &[0xABu8; 32], 528);
    }

    #[test]
    fn missing_parent_root_writes_32_zeros() {
        let envelope = envelope_from_json(V4_JSON, None);
        assert_parent_then_ssz(&envelope, &[0u8; 32], 560);
    }
}
