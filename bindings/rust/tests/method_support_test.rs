use colibri::{get_method_support, get_method_type, MethodType};

#[test]
fn test_eth_block_number_support() {
    let support = get_method_support(1, "eth_blockNumber").unwrap();
    assert!(support >= 0, "eth_blockNumber should be supported");

    let method_type = get_method_type(1, "eth_blockNumber").unwrap();
    assert_ne!(method_type, MethodType::NotSupported);
}

#[test]
fn test_eth_get_balance_support() {
    let support = get_method_support(1, "eth_getBalance").unwrap();
    assert!(support >= 0, "eth_getBalance should be supported");
}

#[test]
fn test_eth_get_block_by_number_support() {
    let support = get_method_support(1, "eth_getBlockByNumber").unwrap();
    assert!(support >= 0, "eth_getBlockByNumber should be supported");
}

#[test]
fn test_eth_get_block_by_hash_support() {
    let support = get_method_support(1, "eth_getBlockByHash").unwrap();
    assert!(support >= 0, "eth_getBlockByHash should be supported");
}

#[test]
fn test_eth_get_transaction_by_hash_support() {
    let support = get_method_support(1, "eth_getTransactionByHash").unwrap();
    assert!(support >= 0, "eth_getTransactionByHash should be supported");
}

#[test]
fn test_eth_get_transaction_receipt_support() {
    let support = get_method_support(1, "eth_getTransactionReceipt").unwrap();
    assert!(support >= 0, "eth_getTransactionReceipt should be supported");
}

#[test]
fn test_eth_call_support() {
    let support = get_method_support(1, "eth_call").unwrap();
    assert!(support >= 0, "eth_call should be supported");
}

#[test]
fn test_eth_get_code_support() {
    let support = get_method_support(1, "eth_getCode").unwrap();
    assert!(support >= 0, "eth_getCode should be supported");
}

#[test]
fn test_eth_get_storage_at_support() {
    let support = get_method_support(1, "eth_getStorageAt").unwrap();
    assert!(support >= 0, "eth_getStorageAt should be supported");
}

#[test]
fn test_eth_get_logs_support() {
    let support = get_method_support(1, "eth_getLogs").unwrap();
    assert!(support >= 0, "eth_getLogs should be supported");
}

#[test]
fn test_unsupported_method() {
    let support = get_method_support(1, "invalid_method_name").unwrap();
    // Unsupported methods typically return -1 or 0
    assert!(support <= 0, "Invalid method should not be supported");
}

#[test]
fn test_method_support_different_chains() {
    let mainnet = get_method_support(1, "eth_blockNumber").unwrap();
    assert!(mainnet >= 0, "Should work on mainnet");

    let sepolia = get_method_support(11155111, "eth_blockNumber").unwrap();
    assert!(sepolia >= 0, "Should work on sepolia");

    let gnosis = get_method_support(100, "eth_blockNumber").unwrap();
    assert!(gnosis >= 0, "Should work on gnosis");
}

#[test]
fn test_method_with_empty_string() {
    let support = get_method_support(1, "").unwrap();
    assert!(support <= 0, "Empty method should not be supported");
}

#[test]
fn test_method_type_enum() {
    let method_type = get_method_type(1, "invalid_method").unwrap();
    assert_eq!(method_type, MethodType::NotSupported);
    assert!(!method_type.is_supported());
    assert!(!method_type.requires_proof());

    let method_type = get_method_type(1, "eth_blockNumber").unwrap();
    assert_eq!(method_type, MethodType::Proofable);
    assert!(method_type.is_supported());
    assert!(method_type.requires_proof());
}

#[test]
fn test_client_method_support() {
    use colibri::ColibriClient;

    let client = ColibriClient::new(None, None);

    let method_type = client.get_method_support("eth_blockNumber").unwrap();
    assert!(method_type.is_supported());

    let method_type = client.get_method_support("invalid_method").unwrap();
    assert_eq!(method_type, MethodType::NotSupported);
}