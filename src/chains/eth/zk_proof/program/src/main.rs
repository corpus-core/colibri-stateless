#![no_main]
sp1_zkvm::entrypoint!(main);

mod bls;

use bls::verify_signature;
use eth_sync_common::merkle::{create_root_hash, sha256_merkle, verify_merkle_proof, verify_slot};
use eth_sync_common::{SP1GuestInput, VerificationOutput};
use sha2::{Digest, Sha256};

fn count_bits_set_512(bits: &[u8; 64]) -> u32 {
    bits.iter().map(|b| b.count_ones()).sum()
}

fn floorlog2_u32(x: u32) -> u32 {
    if x == 0 {
        panic!("floorlog2(0) is undefined");
    }
    31 - x.leading_zeros()
}

fn ssz_add_gindex(parent: u32, child: u32) -> u32 {
    // Compose generalized indices: `parent` points to the root of a subtree and `child` is a gindex
    // inside that subtree. Result is the gindex in the global tree.
    //
    // This matches the generalized index composition used by SSZ proofs.
    let depth = floorlog2_u32(child);
    let base = 1u32.checked_shl(depth).expect("gindex depth overflow");
    parent
        .checked_shl(depth)
        .and_then(|p| p.checked_add(child - base))
        .expect("gindex overflow")
}

const NEXT_SYNC_COMMITTEE_GINDEX_DENEB: u32 = 55;
const NEXT_SYNC_COMMITTEE_GINDEX_ELECTRA: u32 = 87;

// Generalized index of `SigningData.BeaconBlockHeader.stateRoot` in our `SigningData` container layout:
// `SigningData = { BeaconBlockHeader, domain }` and `BeaconBlockHeader = { slot, proposer_index, parent_root, state_root, body_root }`.
const SIGNING_DATA_STATE_ROOT_GINDEX: u32 = 19;

/// The main entrypoint for the SP1 Guest Program.
///
/// This program verifies an Ethereum Sync Committee Light Client Update.
/// It supports two modes:
/// 1. **Direct Verification**: Verifies a single period transition (Period N -> Period N+1).
/// 2. **Recursive Verification**: Verifies a previous SP1 proof (covering Anchor -> Period N)
///    AND the current transition (Period N -> Period N+1), outputting an aggregated proof
///    (Anchor -> Period N+1).
pub fn main() {
    // Read the input structure from the host
    let input = sp1_zkvm::io::read::<SP1GuestInput>();
    let proof_data = input.proof_data;

    // Initialize output current keys with the current step's input (Default: Step-by-step)
    // If recursion is active, this will be overwritten by the previous proof's start root.
    let mut output_current_keys_root = proof_data.current_keys_root;

    // --- RECURSION LOGIC ---
    if let Some(rec_data) = input.recursion_data {
        // 1. Verify the previous SP1 proof
        // This syscall checks that 'rec_data.vkey_hash' (the program that ran)
        // and 'rec_data.public_values_digest' (the output it claimed) are valid.
        sp1_zkvm::lib::verify::verify_sp1_proof(
            &rec_data.vkey_hash,
            &rec_data.public_values_digest,
        );

        // 2. Verify that the provided full 'public_values' match the verified digest
        let mut hasher = Sha256::new();
        hasher.update(&rec_data.public_values);
        let hash_result = hasher.finalize();
        let calculated_digest: [u8; 32] = hash_result.into();

        if calculated_digest != rec_data.public_values_digest {
            panic!("Public Values Hash Mismatch");
        }

        // 3. Deserialize the previous proof's output
        let prev_output: VerificationOutput = bincode::deserialize(&rec_data.public_values)
            .expect("Failed to deserialize prev public values");

        // AGGREGATION LOGIC:
        // If we are verifying recursively, our "start state" is the start state of the previous proof.
        // This allows us to prove a chain A->B->C with a single proof that attests "A->C".
        output_current_keys_root = prev_output.current_keys_root;

        // 4. Chain Continuity Checks

        // Calculate current period from the slot in the header
        // Slot is divided by 32 (slots per epoch) * 256 (epochs per period) = 8192
        let current_period = u64::from_le_bytes(proof_data.slot_bytes) >> 13; // / 8192

        // Check Period Continuity: The previous proof must have output the period we are currently in.
        if prev_output.next_period != current_period {
            panic!(
                "Period mismatch: Prev target {} != Current {}",
                prev_output.next_period, current_period
            );
        }

        // Check Key Continuity: The 'next_keys' from the previous proof must match our 'current_keys'.
        if prev_output.next_keys_root != proof_data.current_keys_root {
            panic!("Key mismatch: Chain broken");
        }
    }

    let slot = u64::from_le_bytes(proof_data.slot_bytes);

    // --- CORE VERIFICATION LOGIC ---

    // 1. Verify period consistency
    // The slot must belong to the period immediately preceding 'next_period'.
    let period = slot >> 13;
    if period != proof_data.next_period - 1 {
        panic!(
            "Invalid period: {} vs {}",
            period,
            proof_data.next_period - 1
        );
    }

    // 2. Verify slot and proposer index
    // The Merkle proof must connect the BeaconHeader to the state root.
    // The BeaconHeader starts with [slot, proposer_index, ...].
    // We verify that hash(slot, proposer) matches the first node in the provided proof branch.
    if proof_data.proof.len() < 96 {
        panic!("Proof too short");
    }
    // Extract the relevant proof node (at the correct depth/position)
    let proof_slice_start = proof_data.proof.len() - 96;
    let proof_element: [u8; 32] = proof_data.proof[proof_slice_start..proof_slice_start + 32]
        .try_into()
        .unwrap();

    if !verify_slot(
        &proof_data.slot_bytes,
        &proof_data.proposer_bytes,
        &proof_element,
    ) {
        panic!("Invalid slot verification");
    }

    // 3. Verify current keys root hash
    // Reconstruct the SSZ root of the provided 512 keys and ensure it matches the expected root.
    // This proves that the keys we use for signature verification are indeed the trusted committee.
    let mut calculated_root = [0u8; 32];
    create_root_hash(&proof_data.current_keys, &mut calculated_root);
    if calculated_root != proof_data.current_keys_root {
        panic!("Invalid current keys root");
    }

    // 4a. Derive and enforce the expected generalized index for `nextSyncCommittee.pubkeys`.
    //
    // We intentionally do **not** derive fork_version/domain inside the zk proof (Option 2).
    // To keep the guest program chain-agnostic (stable VK across new chains), we select the
    // applicable SSZ layout by the Merkle proof depth:
    // - Deneb and earlier: next_sync_committee gindex = 55  -> total path depth 10
    // - Electra and later: next_sync_committee gindex = 87 -> total path depth 11
    //
    // This still binds the statement to `next_sync_committee.pubkeys` (not an arbitrary leaf),
    // but leaves "which fork/domain is correct for the chain" to be verified outside.
    let next_sync_committee_gindex = match proof_data.proof.len() / 32 {
        10 => NEXT_SYNC_COMMITTEE_GINDEX_DENEB,
        11 => NEXT_SYNC_COMMITTEE_GINDEX_ELECTRA,
        n => panic!("Unexpected Merkle proof depth: {} nodes", n),
    };

    let expected_gidx =
        ssz_add_gindex(SIGNING_DATA_STATE_ROOT_GINDEX, next_sync_committee_gindex) * 2;

    // 4. Verify Merkle Proof: NextKeysRoot -> SigningRoot
    // 'root' starts as the leaf (next_keys_root) and is hashed up the tree using the 'proof'.
    // The result must match the SigningRoot (which acts as the message for the BLS signature).
    let mut root = proof_data.next_keys_root;
    verify_merkle_proof(&proof_data.proof, expected_gidx, &mut root);

    // Note: Domain correctness (fork_version/genesis_validators_root binding) is verified outside
    // of the zk proof in this design.

    // 4b. Compute `attested_header_root` (hash_tree_root of `BeaconBlockHeader`) for WS checks.
    //
    // Our proof layout is produced by `proof_sync.c`:
    // - 1 node: aggregatePubkey helper (pubkeys_root -> sync_committee_root)
    // - 5 or 6 nodes: nextSyncCommitteeBranch (sync_committee_root -> attested_state_root)
    // - 4 nodes: header proof (attested_state_root -> SigningData root)
    //
    // This lets us recover:
    // - `attested_state_root` as the intermediate root after the first (len-4) proof nodes
    // - `attested_header_root` as the intermediate root at gindex 2 while traversing the header proof
    let total_nodes = proof_data.proof.len() / 32;
    if total_nodes != 10 && total_nodes != 11 {
        panic!("Unexpected proof node count: {}", total_nodes);
    }
    let header_proof_nodes = 4usize;
    let state_root_nodes = total_nodes - header_proof_nodes; // 6 (Deneb) or 7 (Electra)

    let mut g = expected_gidx;
    let mut attested_state_root = proof_data.next_keys_root;
    for i in 0..state_root_nodes {
        let start = i * 32;
        let sibling: [u8; 32] = proof_data.proof[start..start + 32].try_into().unwrap();
        if g & 1 == 1 {
            attested_state_root = sha256_merkle(&sibling, &attested_state_root);
        } else {
            attested_state_root = sha256_merkle(&attested_state_root, &sibling);
        }
        g >>= 1;
    }
    if g != SIGNING_DATA_STATE_ROOT_GINDEX {
        panic!(
            "Internal error: expected to reach gindex {} after state proof, got {}",
            SIGNING_DATA_STATE_ROOT_GINDEX, g
        );
    }

    let header_proof_start = state_root_nodes * 32;
    let header_proof = &proof_data.proof[header_proof_start..];
    if header_proof.len() != header_proof_nodes * 32 {
        panic!("Unexpected header proof length: {}", header_proof.len());
    }

    // The last sibling on the path from `SigningData.BeaconBlockHeader.stateRoot` to the `SigningData`
    // root is the `domain` leaf itself (SigningData has two fields: header + domain).
    let domain: [u8; 32] = header_proof[header_proof.len() - 32..].try_into().unwrap();
    if domain[0..4] != [0x07, 0x00, 0x00, 0x00] {
        panic!("Invalid domain type: expected DOMAIN_SYNC_COMMITTEE");
    }

    let mut header_g = SIGNING_DATA_STATE_ROOT_GINDEX;
    let mut attested_header_root = attested_state_root;
    // Traverse 3 levels to reach BeaconBlockHeader root at gindex 2.
    for i in 0..3usize {
        let sibling: [u8; 32] = header_proof[i * 32..i * 32 + 32].try_into().unwrap();
        if header_g & 1 == 1 {
            attested_header_root = sha256_merkle(&sibling, &attested_header_root);
        } else {
            attested_header_root = sha256_merkle(&attested_header_root, &sibling);
        }
        header_g >>= 1;
    }
    if header_g != 2 {
        panic!(
            "Internal error: expected to reach BeaconBlockHeader root at gindex 2, got {}",
            header_g
        );
    }

    // 5. Verify BLS Signature
    // Checks that the aggregated signature is valid for the 'root' (message)
    // and the subset of 'current_keys' indicated by 'signature_bits'.
    let signature_array: [u8; 96] = proof_data
        .signature
        .as_slice()
        .try_into()
        .expect("Signature must be 96 bytes");
    let signature_bits_array: [u8; 64] = proof_data
        .signature_bits
        .as_slice()
        .try_into()
        .expect("Signature bits must be 64 bytes");

    // Spec-level sanity: require at least one participant (MIN_SYNC_COMMITTEE_PARTICIPANTS = 1).
    // This prevents the edge case where an empty aggregate key and infinity signature could pass.
    let participants = count_bits_set_512(&signature_bits_array);
    if participants < 1 {
        panic!("Invalid sync committee participation: 0 participants");
    }

    // Enforce 2/3 participation threshold for accepting the update as a period transition.
    // This matches the light client acceptance threshold logic (supermajority).
    if participants * 3 < 512 * 2 {
        panic!(
            "Insufficient sync committee participation: {} / 512",
            participants
        );
    }

    if !verify_signature(
        &root,
        &signature_array,
        &proof_data.current_keys,
        &signature_bits_array,
    ) {
        panic!("Invalid signature verification");
    }

    // --- OUTPUT COMMITMENT ---

    let output = VerificationOutput {
        current_keys_root: output_current_keys_root, // Aggregated start (A) or Current start (N)
        next_keys_root: proof_data.next_keys_root,   // New End (N+1)
        next_period: proof_data.next_period,         // Period Number
        attested_header_root,
        domain,
    };

    sp1_zkvm::io::commit(&output);
}
