//! End-to-end tests for the split `create_proof` / `verify_proof`
//! path. The `rpc()` fixture loop in `integration_test.rs` covers the
//! fused call, but hosts that build proofs offline and verify them
//! later (e.g. a signing service) rely on the two-step form. Regression
//! coverage for empty / invalid proofs lives here too.

use std::sync::Arc;

use colibri_stateless::testing::{
    discover_tests, find_test_data_root, FileBackedMockRequestHandler, FileBackedMockStorage,
    TestCase,
};
use colibri_stateless::{reset_caches, Colibri, ColibriError, PrivacyMode};
use serial_test::serial;

const FIXTURE_NAME: &str = "eth_getBalance1";

fn build_client(tc: &TestCase) -> Colibri {
    reset_caches();
    let mock_storage = FileBackedMockStorage::new(tc.directory.clone());
    let handler = Arc::new(FileBackedMockRequestHandler::new(tc.directory.clone()));

    let provers = if tc.remote_prover {
        vec!["http://mock-prover".to_string()]
    } else {
        Vec::<String>::new()
    };

    Colibri::builder(tc.chain_id)
        .provers(provers)
        .eth_rpcs(Vec::<String>::new())
        .beacon_apis(Vec::<String>::new())
        .checkpointz(Vec::<String>::new())
        .include_code(tc.include_code)
        .use_accesslist(tc.use_accesslist)
        .privacy_mode(if tc.pap {
            PrivacyMode::Basic
        } else {
            PrivacyMode::None
        })
        .max_latest_age_seconds(0)
        .storage(mock_storage)
        .request_handler(handler)
        .build()
}

fn load_fixture(name: &str) -> Option<TestCase> {
    let root = find_test_data_root()?;
    discover_tests(root).into_iter().find(|tc| tc.name == name)
}

/// A fixture is fully described by (proof bytes, expected result).
/// `create_proof` must produce non-empty bytes and `verify_proof` on
/// those bytes must reproduce the expected result exactly.
#[tokio::test(flavor = "current_thread")]
#[serial]
async fn create_and_verify_proof_roundtrip() {
    let Some(tc) = load_fixture(FIXTURE_NAME) else {
        eprintln!("fixture {FIXTURE_NAME} missing; skipping");
        return;
    };
    assert!(
        tc.has_expected_result,
        "roundtrip fixture must have expected_result"
    );

    let client = build_client(&tc);

    let proof = client
        .create_proof(&tc.method, &tc.params)
        .await
        .expect("create_proof");
    assert!(!proof.is_empty(), "prover returned empty proof");

    let result = client
        .verify_proof(&proof, &tc.method, &tc.params)
        .await
        .expect("verify_proof");
    assert_eq!(result, tc.expected_result);
}

/// `verify_proof` on an empty byte slice must fail closed -- we
/// specifically want a `Verification` / `Ffi` error, not a panic or a
/// silent success.
#[tokio::test(flavor = "current_thread")]
#[serial]
async fn verify_proof_rejects_empty_proof() {
    let Some(tc) = load_fixture(FIXTURE_NAME) else {
        eprintln!("fixture {FIXTURE_NAME} missing; skipping");
        return;
    };
    let client = build_client(&tc);

    let err = client
        .verify_proof(&[], &tc.method, &tc.params)
        .await
        .expect_err("empty proof must be rejected");
    assert!(
        matches!(
            err,
            ColibriError::Verification(_) | ColibriError::Ffi(_) | ColibriError::Proof(_)
        ),
        "unexpected error kind for empty proof: {err:?}"
    );
}

/// `verify_proof` on garbled bytes must fail closed too.
#[tokio::test(flavor = "current_thread")]
#[serial]
async fn verify_proof_rejects_garbled_proof() {
    let Some(tc) = load_fixture(FIXTURE_NAME) else {
        eprintln!("fixture {FIXTURE_NAME} missing; skipping");
        return;
    };
    let client = build_client(&tc);

    let junk = vec![0xAAu8; 1024];
    let err = client
        .verify_proof(&junk, &tc.method, &tc.params)
        .await
        .expect_err("garbled proof must be rejected");
    assert!(
        matches!(
            err,
            ColibriError::Verification(_) | ColibriError::Ffi(_) | ColibriError::Proof(_)
        ),
        "unexpected error kind for garbled proof: {err:?}"
    );
}
