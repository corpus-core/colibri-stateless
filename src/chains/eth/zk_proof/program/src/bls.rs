use bls12_381::{G1Affine, G1Projective, G2Affine, G2Projective};
use bls12_381::hash_to_curve::{ExpandMsgXmd, HashToCurve};
use bls12_381::{pairing, Gt};
use group::{Curve, Group};

pub fn verify_signature(
    message_hash: &[u8; 32],
    signature_bytes: &[u8; 96],
    public_keys: &[u8], // 512 * 48
    pubkeys_used: &[u8; 64]
) -> bool {
    // 1. Aggregate public keys
    let mut pubkey_sum = G1Projective::identity();
    // let mut first = true; // G1Projective::identity() handles the first element logic implicitly by being 0
    
    for i in 0..512 {
        let byte_idx = i / 8;
        let bit_idx = i % 8;
        
        if (pubkeys_used[byte_idx] & (1 << bit_idx)) != 0 {
            let offset = i * 48;
            if offset + 48 > public_keys.len() { return false; }
            
            let pk_bytes: [u8; 48] = public_keys[offset..offset+48].try_into().unwrap();
            let pk_opt = G1Affine::from_compressed(&pk_bytes);
            // from_compressed returns CtOption. 
            // We need to check if it's valid.
            if bool::from(pk_opt.is_some()) {
                pubkey_sum += pk_opt.unwrap();
            } else {
                return false;
            }
        }
    }
    
    let pubkey_aggregated = pubkey_sum.to_affine();
    
    // 2. Deserialize signature
    let sig_opt = G2Affine::from_compressed(signature_bytes);
    if !bool::from(sig_opt.is_some()) { return false; }
    let sig = sig_opt.unwrap();
    
    // 3. Verify
    // Domain Separation Tag for Ethereum
    let dst = b"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
    
    // Hash message to G2
    let msg_on_curve = <G2Projective as HashToCurve<ExpandMsgXmd<sha2::Sha256>>>::hash_to_curve(message_hash, dst).to_affine();
    
    // Pairing check: e(PK_agg, H(m)) == e(G1, Sig)
    // We check e(PK_agg, H(m)) == e(g1_generator, sig)
    
    let g1_gen = G1Affine::generator();
    
    let left = pairing(&pubkey_aggregated, &msg_on_curve);
    let right = pairing(&g1_gen, &sig);
    
    left == right
}


