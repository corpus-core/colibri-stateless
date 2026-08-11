//! Local checks around `c4_get_method_support` -- these don't require
//! fixtures or the network.

use colibri_stateless::{get_method_support, get_method_type, MethodType};

fn assert_supported(chain_id: u64, method: &str) {
    let ty = get_method_type(chain_id, method).unwrap();
    assert!(
        ty.is_supported(),
        "{method} should be supported on chain {chain_id} (got {:?})",
        ty
    );
}

#[test]
fn common_proofable_methods_are_supported() {
    for m in [
        "eth_blockNumber",
        "eth_getBalance",
        "eth_getBlockByNumber",
        "eth_getBlockByHash",
        "eth_getTransactionByHash",
        "eth_getTransactionReceipt",
        "eth_call",
        "eth_getCode",
        "eth_getStorageAt",
        "eth_getLogs",
    ] {
        assert_supported(1, m);
    }
}

#[test]
fn different_chains_share_supported_methods() {
    for chain in [1u64, 11_155_111, 100, 10_200] {
        assert_supported(chain, "eth_blockNumber");
    }
}

#[test]
fn empty_or_unknown_method_is_not_supported() {
    // Unknown / unregistered methods are classified as `Undefined`
    // (returned by the C core), which `is_supported()` treats as
    // not-supported.
    for method in ["", "invalid_method_name"] {
        let ty = get_method_type(1, method).unwrap();
        assert!(
            !ty.is_supported(),
            "{method:?} should not be supported ({ty:?})"
        );
    }
}

#[test]
fn support_helper_returns_raw_code() {
    // MethodType::Proofable is `1` on the C side.
    let ty = get_method_support(1, "eth_blockNumber", None, 0).unwrap();
    assert_eq!(ty, MethodType::Proofable);
}
