//! `decode_proof`: a stored proof from the shared fixtures decodes into
//! JSON; bytes that aren't a valid proof decode into nothing.

use colibri_stateless::decode_proof;
use std::path::PathBuf;

fn fixture(name: &str) -> Vec<u8> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../test/data")
        .join(name);
    std::fs::read(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

#[test]
fn stored_proof_decodes_into_json() {
    let text = decode_proof(&fixture("eth_getBlockHeader1/proof.ssz")).expect("a valid proof");
    let value: serde_json::Value = serde_json::from_str(&text).expect("JSON");
    let object = value.as_object().expect("a JSON object");
    assert!(
        object.contains_key("proof"),
        "fields: {:?}",
        object.keys().collect::<Vec<_>>()
    );
}

#[test]
fn invalid_bytes_decode_into_nothing() {
    assert_eq!(decode_proof(&[]), None);
    let mut truncated = fixture("eth_getBlockHeader1/proof.ssz");
    truncated.truncate(truncated.len() / 2);
    assert_eq!(decode_proof(&truncated), None);
}
