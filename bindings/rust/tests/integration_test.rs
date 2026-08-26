//! Shared-fixture integration tests. Runs every fixture under
//! `test/data/*` through the unified RPC entry point and compares the
//! result against the recorded `expected_result`.
//!
//! Mirrors [`bindings/python/tests/test_integration_root.py`][py] and
//! [`bindings/dart/test/integration_test.dart`][dart] so all bindings
//! share the same fixture surface.
//!
//! Because the C core keeps its storage callback in a global, all
//! fixtures share the same slot; every test runs `#[serial]` to keep
//! them from stepping on each other.
//!
//! [py]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/python/tests/test_integration_root.py
//! [dart]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/dart/test/integration_test.dart

use std::sync::Arc;

use colibri_stateless::testing::{
    discover_tests, find_test_data_root, FileBackedMockRequestHandler, FileBackedMockStorage,
    TestCase,
};
use colibri_stateless::{reset_caches, Colibri, ColibriError, PrivacyMode};
use serial_test::serial;

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
        // No real HTTP is executed; keep the endpoint lists empty so
        // any accidental miss lands in the mock handler's "no fixture"
        // error path.
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
        // Recorded fixtures always drift out of the `latest`
        // freshness window; the freshness check itself is exercised
        // separately in `test/unittests/test_verify_call_freshness.c`.
        .max_latest_age_seconds(0)
        .storage(mock_storage)
        .request_handler(handler)
        .build()
}

async fn run_case(tc: TestCase) -> Result<serde_json::Value, ColibriError> {
    let client = build_client(&tc);
    client.rpc(&tc.method, &tc.params).await
}

/// `expected_result: null` must be *validated*, not silently
/// skipped. Regression coverage for the two-pass parse in
/// `discover_tests`.
#[test]
fn expected_result_null_is_recognised() {
    let Some(root) = find_test_data_root() else {
        eprintln!("test/data not found -- crate is out of tree, skipping");
        return;
    };
    let Some(tc) = discover_tests(&root)
        .into_iter()
        .find(|tc| tc.name == "pap_tx_pending")
    else {
        eprintln!("pap_tx_pending fixture missing -- skipping");
        return;
    };
    assert!(
        tc.has_expected_result,
        "pap_tx_pending must carry an expected_result key even when it is null"
    );
    assert_eq!(
        tc.expected_result,
        serde_json::Value::Null,
        "pap_tx_pending expected null result"
    );
}

#[test]
fn discovers_test_fixtures() {
    let Some(root) = find_test_data_root() else {
        eprintln!("test/data not found -- crate is out of tree, skipping");
        return;
    };
    let cases = discover_tests(&root);
    assert!(
        !cases.is_empty(),
        "expected at least one fixture under {}",
        root.display()
    );
}

/// Runs the whole fixture corpus in one test to keep the global storage
/// slot serial without needing per-fixture `#[test]` functions. This
/// mirrors how the Dart / Python suites run their loops.
#[tokio::test(flavor = "current_thread")]
#[serial]
async fn all_fixtures() {
    let Some(root) = find_test_data_root() else {
        eprintln!("test/data not found -- crate is out of tree, skipping");
        return;
    };
    let cases = discover_tests(&root);
    if cases.is_empty() {
        panic!("no fixtures discovered under {}", root.display());
    }

    let mut passed = 0usize;
    let mut skipped = 0usize;
    let mut failures = Vec::<String>::new();
    for tc in cases {
        let name = tc.name.clone();
        let has_expected = tc.has_expected_result;
        let expected = tc.expected_result.clone();
        eprintln!("--- {} ({}) chain={} ---", name, tc.method, tc.chain_id);
        match run_case(tc).await {
            Ok(result) => {
                if !has_expected {
                    skipped += 1;
                    eprintln!("  (skipped: fixture omits expected_result)");
                    continue;
                }
                if result == expected {
                    passed += 1;
                    eprintln!("  ok");
                } else {
                    failures.push(format!(
                        "{name}: mismatch (got {result}, expected {expected})"
                    ));
                }
            }
            Err(e) => {
                failures.push(format!("{name}: error: {e}"));
            }
        }
    }

    eprintln!(
        "\nfixtures: {} passed, {} skipped (no expected), {} failed",
        passed,
        skipped,
        failures.len()
    );
    for f in &failures {
        eprintln!("  {f}");
    }
    assert!(failures.is_empty(), "{} fixture(s) failed", failures.len());
}
