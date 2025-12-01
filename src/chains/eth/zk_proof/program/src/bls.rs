use bls12_381::{G1Affine, G1Projective, G2Affine, G2Projective};
use bls12_381::hash_to_curve::{ExpandMsgXmd, HashToCurve};
use bls12_381::{pairing, Gt};
use group::{Curve, Group};

/// Verifies an aggregated BLS signature for the Ethereum Sync Committee.
///
/// # Arguments
/// * `message_hash` - The 32-byte hash of the message (Signing Root) being verified.
/// * `signature_bytes` - The 96-byte aggregated BLS signature (G2 Point).
/// * `public_keys` - The flat byte array containing all 512 public keys (512 * 48 bytes).
/// * `pubkeys_used` - A 64-byte bitfield indicating which of the 512 keys participated.
///
/// # Returns
/// `true` if the signature is valid, `false` otherwise.
pub fn verify_signature(
    message_hash: &[u8; 32],
    signature_bytes: &[u8; 96],
    public_keys: &[u8], // 512 * 48
    pubkeys_used: &[u8; 64]
) -> bool {
    // 1. Aggregate Public Keys
    // We only sum the public keys of validators who participated in the signature.
    let mut pubkey_sum = G1Projective::identity();
    
    for i in 0..512 {
        // Check if the i-th bit is set in the bitfield
        let byte_idx = i / 8;
        let bit_idx = i % 8;
        
        if (pubkeys_used[byte_idx] & (1 << bit_idx)) != 0 {
            let offset = i * 48;
            if offset + 48 > public_keys.len() { return false; }
            
            // Extract 48-byte compressed G1 point
            let pk_bytes: [u8; 48] = public_keys[offset..offset+48].try_into().unwrap();
            
            // Decompress G1 Point
            let pk_opt = G1Affine::from_compressed(&pk_bytes);
            
            // Add to sum if valid
            if bool::from(pk_opt.is_some()) {
                pubkey_sum += pk_opt.unwrap();
            } else {
                // Invalid public key in the committee -> Proof is invalid
                return false;
            }
        }
    }
    
    // Convert the aggregated sum to Affine form for pairing
    let pubkey_aggregated = pubkey_sum.to_affine();
    
    // 2. Deserialize Signature
    // The signature is a point on G2
    let sig_opt = G2Affine::from_compressed(signature_bytes);
    if !bool::from(sig_opt.is_some()) { return false; }
    let sig = sig_opt.unwrap();
    
    // 3. Perform Pairing Check
    //
    // Equation: e(PK_agg, H(m)) == e(G1_gen, Sig)
    //
    // Note on Ethereum's setup:
    // - Public Keys are on G1
    // - Signatures are on G2
    // - This is swapped relative to Zcash/standard BLS12-381, but the library handles it.
    
    // Domain Separation Tag (DST) for Ethereum
    let dst = b"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
    
    // Hash the message (Signing Root) to a point on G2
    let msg_on_curve = <G2Projective as HashToCurve<ExpandMsgXmd<sha2::Sha256>>>::hash_to_curve(message_hash, dst).to_affine();
    
    // We verify: e(PK_agg, H(m)) == e(G1, Sig)
    // Left side: Pairing of aggregated pubkey (G1) and hashed message (G2)
    // Right side: Pairing of Generator (G1) and Signature (G2)
    
    let g1_gen = G1Affine::generator();
    
    let left = pairing(&pubkey_aggregated, &msg_on_curve);
    let right = pairing(&g1_gen, &sig);
    
    left == right
}


