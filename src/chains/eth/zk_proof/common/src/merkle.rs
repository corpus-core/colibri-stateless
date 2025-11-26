use sha2::{Sha256, Digest};

pub fn sha256_merkle(left: &[u8; 32], right: &[u8; 32]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(left);
    hasher.update(right);
    hasher.finalize().into()
}

pub fn verify_merkle_proof(proof: &[u8], mut gindex: u32, root: &mut [u8; 32]) {
    for chunk in proof.chunks(32) {
        if chunk.len() != 32 {
            break; 
        }
        let sibling: [u8; 32] = chunk.try_into().unwrap();
        if gindex & 1 == 1 {
            *root = sha256_merkle(&sibling, root);
        } else {
            *root = sha256_merkle(root, &sibling);
        }
        gindex >>= 1;
    }
}

pub fn _root_hash(keys: &[u8], out: &mut [u8; 32], gindex: u32) {
    if gindex >= 512 {
        let offset = (gindex - 512) as usize * 48;
        if offset + 48 > keys.len() {
             return;
        }
        let mut left = [0u8; 32];
        let mut right = [0u8; 32];
        
        left.copy_from_slice(&keys[offset..offset+32]);
        // right is first 16 bytes of key + 16 bytes of zeros
        right[0..16].copy_from_slice(&keys[offset+32..offset+48]);
        
        *out = sha256_merkle(&left, &right);
    } else {
        let mut left = [0u8; 32];
        let mut right = [0u8; 32];
        _root_hash(keys, &mut left, gindex * 2);
        _root_hash(keys, &mut right, gindex * 2 + 1);
        *out = sha256_merkle(&left, &right);
    }
}

pub fn create_root_hash(keys: &[u8], out: &mut [u8; 32]) {
    _root_hash(keys, out, 1);
}

pub fn verify_slot(slot: &[u8; 8], proposer: &[u8; 8], proof: &[u8; 32]) -> bool {
    let mut slot_hash = [0u8; 32];
    let mut proposer_hash = [0u8; 32];
    
    slot_hash[0..8].copy_from_slice(slot);
    proposer_hash[0..8].copy_from_slice(proposer);
    
    let calculated = sha256_merkle(&slot_hash, &proposer_hash);
    calculated == *proof
}
