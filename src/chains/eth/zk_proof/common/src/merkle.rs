use sha2::{Sha256, Digest};

/// Computes the SHA-256 Merkle hash of two 32-byte nodes.
/// Returns SHA256(left || right).
pub fn sha256_merkle(left: &[u8; 32], right: &[u8; 32]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(left);
    hasher.update(right);
    hasher.finalize().into()
}

/// Verifies a Merkle proof against a root.
///
/// This function reconstructs the root hash by traversing the proof path
/// from the leaf up to the root.
///
/// # Arguments
/// * `proof` - A byte slice containing the sibling nodes (chunks of 32 bytes).
/// * `gindex` - The generalized index of the leaf being verified. This determines
///              the path (left/right) at each level.
/// * `root` - A mutable reference to the starting leaf hash.
///            **On return, this will contain the computed root hash.**
pub fn verify_merkle_proof(proof: &[u8], mut gindex: u32, root: &mut [u8; 32]) {
    for chunk in proof.chunks(32) {
        if chunk.len() != 32 {
            break; 
        }
        let sibling: [u8; 32] = chunk.try_into().unwrap();
        if gindex & 1 == 1 {
            // Current node is right child, sibling is left
            *root = sha256_merkle(&sibling, root);
        } else {
            // Current node is left child, sibling is right
            *root = sha256_merkle(root, &sibling);
        }
        gindex >>= 1;
    }
}

/// Internal recursive helper to calculate the SSZ HashTreeRoot of a list of public keys.
///
/// # Arguments
/// * `keys` - The full byte slice of all public keys (must be 512 * 48 bytes).
/// * `out` - The output buffer for the computed hash of the current subtree.
/// * `gindex` - The current generalized index in the tree (starts at 1 for root).
pub fn _root_hash(keys: &[u8], out: &mut [u8; 32], gindex: u32) {
    if gindex >= 512 {
        // Leaf node: Hash the pubkey (padded to 64 bytes for SSZ chunking)
        let offset = (gindex - 512) as usize * 48;
        if offset + 48 > keys.len() {
             return;
        }
        let mut left = [0u8; 32];
        let mut right = [0u8; 32];
        
        // Pubkey is 48 bytes. SSZ chunks are 32 bytes.
        // Chunk 0: First 32 bytes of pubkey
        left.copy_from_slice(&keys[offset..offset+32]);
        
        // Chunk 1: Remaining 16 bytes of pubkey + 16 bytes padding (zero)
        right[0..16].copy_from_slice(&keys[offset+32..offset+48]);
        
        *out = sha256_merkle(&left, &right);
    } else {
        // Internal node: Hash children
        let mut left = [0u8; 32];
        let mut right = [0u8; 32];
        _root_hash(keys, &mut left, gindex * 2);
        _root_hash(keys, &mut right, gindex * 2 + 1);
        *out = sha256_merkle(&left, &right);
    }
}

/// Calculates the SSZ HashTreeRoot of a Sync Committee (512 BLS Pubkeys).
///
/// This function reconstructs the root hash expected in the Beacon State.
/// It is used to verify that the `current_keys` provided in the input match
/// the trusted `current_keys_root`.
///
/// # Arguments
/// * `keys` - Byte slice containing exactly 512 * 48 bytes of public key data.
/// * `out` - 32-byte buffer to store the resulting root hash.
pub fn create_root_hash(keys: &[u8], out: &mut [u8; 32]) {
    _root_hash(keys, out, 1);
}

/// Verifies a slot and proposer index against a block header root (proof).
///
/// In the Beacon Header, `slot` and `proposer_index` are the first two fields.
/// This function checks if `hash(slot, proposer_index)` matches the provided proof node.
///
/// # Arguments
/// * `slot` - 8-byte little-endian slot number.
/// * `proposer` - 8-byte little-endian proposer index.
/// * `proof` - The expected 32-byte hash (first node of the header tree).
///
/// # Returns
/// `true` if the hash matches, `false` otherwise.
pub fn verify_slot(slot: &[u8; 8], proposer: &[u8; 8], proof: &[u8; 32]) -> bool {
    let mut slot_hash = [0u8; 32];
    let mut proposer_hash = [0u8; 32];
    
    // SSZ encoding: uint64 is just little-endian bytes, padded to 32 bytes for chunking
    slot_hash[0..8].copy_from_slice(slot);
    proposer_hash[0..8].copy_from_slice(proposer);
    
    let calculated = sha256_merkle(&slot_hash, &proposer_hash);
    calculated == *proof
}
