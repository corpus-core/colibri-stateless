use eth_sync_common::{program_hash_from_vkey, RecursionData, VerificationOutput};
use sha2::{Digest, Sha256};

const PROGRAM_KEY: [u32; 8] = [1, 2, 3, 4, 5, 6, 7, 8];
const CURRENT_KEYS: [u8; 32] = [0x42; 32];
const CURRENT_PERIOD: u64 = 1700;

fn child_output(program_hash: [u8; 32]) -> VerificationOutput {
    VerificationOutput {
        current_keys_root: [0x11; 32],
        next_keys_root: CURRENT_KEYS,
        next_period: CURRENT_PERIOD,
        attested_header_root: [0x22; 32],
        domain: [0x33; 32],
        program_hash,
    }
}

fn recursion(output: VerificationOutput) -> RecursionData {
    let public_values = bincode::serialize(&output).unwrap();
    RecursionData {
        public_values_digest: Sha256::digest(&public_values).into(),
        public_values,
    }
}

#[test]
fn same_program_preserves_the_authenticated_anchor() {
    let program_hash = program_hash_from_vkey(&PROGRAM_KEY);
    let previous = recursion(child_output(program_hash)).verify_public_values(
        &program_hash,
        &CURRENT_KEYS,
        CURRENT_PERIOD,
    );
    assert_eq!(previous.current_keys_root, [0x11; 32]);
    assert_eq!(previous.next_keys_root, CURRENT_KEYS);
    assert_eq!(previous.next_period, CURRENT_PERIOD);
    assert_eq!(previous.program_hash, program_hash);
}

#[test]
#[should_panic(expected = "Recursive program identity mismatch")]
fn matching_chain_cannot_relabel_a_different_program() {
    let program_hash = program_hash_from_vkey(&PROGRAM_KEY);
    let mut other_key = PROGRAM_KEY;
    other_key[7] += 1;
    // The digest, period, and committee continuity all agree. Only authorization differs.
    // This also rejects a genuine same-program child created with another authorized-key input.
    recursion(child_output(program_hash_from_vkey(&other_key))).verify_public_values(
        &program_hash,
        &CURRENT_KEYS,
        CURRENT_PERIOD,
    );
}

#[test]
#[should_panic(expected = "Period mismatch")]
fn authorized_identity_does_not_bypass_period_continuity() {
    let program_hash = program_hash_from_vkey(&PROGRAM_KEY);
    recursion(child_output(program_hash)).verify_public_values(
        &program_hash,
        &CURRENT_KEYS,
        CURRENT_PERIOD + 1,
    );
}

#[test]
#[should_panic(expected = "Key mismatch: Chain broken")]
fn authorized_identity_does_not_bypass_committee_continuity() {
    let program_hash = program_hash_from_vkey(&PROGRAM_KEY);
    recursion(child_output(program_hash)).verify_public_values(
        &program_hash,
        &[0x43; 32],
        CURRENT_PERIOD,
    );
}

#[test]
#[should_panic(expected = "Failed to deserialize prev public values")]
fn legacy_child_without_an_identity_commitment_is_rejected() {
    let program_hash = program_hash_from_vkey(&PROGRAM_KEY);
    let mut child = recursion(child_output(program_hash));
    child.public_values.truncate(136);
    child.public_values_digest = Sha256::digest(&child.public_values).into();
    child.verify_public_values(&program_hash, &CURRENT_KEYS, CURRENT_PERIOD);
}

#[test]
#[should_panic(expected = "Non-canonical program key digest")]
fn digest_words_cannot_overlap_into_a_neighboring_word() {
    let mut key = PROGRAM_KEY;
    key[7] |= 1 << 31;
    program_hash_from_vkey(&key);
}
