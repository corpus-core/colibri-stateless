//! `decode_proof`: the shared fixtures' stored proofs decode into JSON
//! objects with a C4Request's fields; bytes that aren't a valid proof
//! decode into nothing.

use colibri_stateless::decode_proof;
use std::path::{Path, PathBuf};

fn stored_proofs() -> Vec<PathBuf> {
    fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
        let Ok(entries) = std::fs::read_dir(dir) else {
            return;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                walk(&path, out);
            } else if path.extension().is_some_and(|e| e == "ssz") {
                out.push(path);
            }
        }
    }
    let mut out = Vec::new();
    walk(
        &Path::new(env!("CARGO_MANIFEST_DIR")).join("../../test/data"),
        &mut out,
    );
    out.sort();
    out
}

#[test]
fn stored_proofs_decode_into_json_objects() {
    let mut decoded = 0;
    for path in stored_proofs() {
        let bytes = std::fs::read(&path).expect("fixture");
        // Not every stored `.ssz` is a whole proof (some are sub-proofs or
        // other SSZ objects); those decode into nothing.
        let Some(text) = decode_proof(&bytes) else {
            continue;
        };
        let value: serde_json::Value = serde_json::from_str(&text)
            .unwrap_or_else(|e| panic!("{}: not JSON: {e}", path.display()));
        let object = value
            .as_object()
            .unwrap_or_else(|| panic!("{}: not a JSON object", path.display()));
        for field in ["data", "proof"] {
            assert!(
                object.contains_key(field),
                "{}: no `{field}`",
                path.display()
            );
        }
        decoded += 1;
    }
    assert!(decoded > 0, "no stored proof decoded");
}

#[test]
fn invalid_bytes_decode_into_nothing() {
    assert_eq!(decode_proof(&[]), None);
    for path in stored_proofs() {
        let mut bytes = std::fs::read(&path).expect("fixture");
        if decode_proof(&bytes).is_none() {
            continue;
        }
        bytes.truncate(bytes.len() / 2);
        assert_eq!(
            decode_proof(&bytes),
            None,
            "{}: half of it decoded",
            path.display()
        );
    }
}
