use colibri::{ColibriClient, ClientConfig, ColibriError, Result, MAINNET, SEPOLIA, GNOSIS};

#[cfg(test)]
mod client_operations {
    use super::*;

    #[tokio::test]
    async fn test_prove_without_urls_should_fail() {
        let config = ClientConfig::new(MAINNET)
            .with_eth_rpcs(vec![])
            .with_beacon_apis(vec![]);
        let client = ColibriClient::new(Some(config), None);

        let result = client.prove("eth_blockNumber", "[]", 1, 0).await;

        assert!(result.is_err(), "Should fail without configured URLs");

        if let Err(e) = result {
            let error_msg = e.to_string();
            assert!(
                error_msg.contains("No servers") || error_msg.contains("failed"),
                "Error should mention missing servers: {}",
                error_msg
            );
        }
    }

    #[tokio::test]
    async fn test_verify_without_urls_should_fail() {
        let config = ClientConfig::new(MAINNET)
            .with_eth_rpcs(vec![])
            .with_beacon_apis(vec![]);
        let client = ColibriClient::new(Some(config), None);
        let fake_proof = vec![0u8; 100];

        let result = client.verify(&fake_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err(), "Should fail without configured URLs");
    }

    #[tokio::test]
    async fn test_prove_with_invalid_method() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.example.com".into()])
            .with_eth_rpcs(vec!["https://rpc.example.com".into()]);
        let client = ColibriClient::new(Some(config), None);

        let result = client.prove("invalid_method", "[]", 1, 0).await;

        assert!(result.is_err(), "Should fail with invalid method");
    }

    #[tokio::test]
    async fn test_verify_with_invalid_proof() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.example.com".into()])
            .with_eth_rpcs(vec!["https://rpc.example.com".into()]);
        let client = ColibriClient::new(Some(config), None);

        let invalid_proof = vec![0xFF; 10];

        let result = client.verify(&invalid_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err(), "Should fail");
    }
}

#[cfg(test)]
mod client_error_handling {
    use super::*;

    #[tokio::test]
    async fn test_error_for_missing_beacon_url() {
        let config = ClientConfig::new(MAINNET)
            .with_eth_rpcs(vec![])
            .with_beacon_apis(vec![]);
        let client = ColibriClient::new(Some(config), None);

        let result = client.prove("eth_blockNumber", "[]", 1, 0).await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_missing_rpc_url() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.test".into()])
            .with_eth_rpcs(vec![]);
        let client = ColibriClient::new(Some(config), None);

        let result = client.prove("eth_getBalance", "[\"0x0\", \"latest\"]", 1, 0).await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_invalid_json_params() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.test".into()])
            .with_eth_rpcs(vec!["https://rpc.test".into()]);
        let client = ColibriClient::new(Some(config), None);

        let result = client.prove("eth_blockNumber", "[invalid json", 1, 0).await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_empty_proof() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.test".into()])
            .with_eth_rpcs(vec!["https://rpc.test".into()]);
        let client = ColibriClient::new(Some(config), None);

        let empty_proof = vec![];

        let result = client.verify(&empty_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_malformed_proof() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.test".into()])
            .with_eth_rpcs(vec!["https://rpc.test".into()]);
        let client = ColibriClient::new(Some(config), None);

        let malformed_proof = vec![0xFF, 0xAA, 0xBB, 0xCC];

        let result = client.verify(&malformed_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err());
    }
}

#[cfg(test)]
mod error_types {
    use super::*;

    #[test]
    fn test_error_display() {
        let error = ColibriError::Ffi("Test error message".to_string());
        let display = format!("{}", error);
        assert_eq!(display, "FFI error: Test error message");
    }

    #[test]
    fn test_error_from_json() {
        let json_str = "invalid json";
        let result: Result<serde_json::Value> = serde_json::from_str(json_str)
            .map_err(ColibriError::from);

        assert!(result.is_err());
        match result {
            Err(ColibriError::Json(_)) => (),
            _ => panic!("Expected JSON error"),
        }
    }

    #[test]
    fn test_error_from_null_pointer() {
        let error = ColibriError::NullPointer;
        let display = format!("{}", error);
        assert_eq!(display, "Null pointer");
    }

    #[test]
    fn test_error_from_cstring() {
        use std::ffi::CString;

        // Try to create a CString with null byte in the middle
        let result = CString::new("hello\0world");

        assert!(result.is_err());
        let error: ColibriError = result.unwrap_err().into();
        let display = format!("{}", error);
        assert!(display.contains("Invalid C string"));
    }
}

#[cfg(test)]
mod client_chain_configurations {
    use super::*;

    #[test]
    fn test_mainnet_configuration() {
        let client = ColibriClient::new(Some(ClientConfig::new(MAINNET)), None);
        assert_eq!(client.chain_id(), MAINNET);
    }

    #[test]
    fn test_sepolia_configuration() {
        let client = ColibriClient::new(Some(ClientConfig::new(SEPOLIA)), None);
        assert_eq!(client.chain_id(), SEPOLIA);
    }

    #[test]
    fn test_gnosis_configuration() {
        let client = ColibriClient::new(Some(ClientConfig::new(GNOSIS)), None);
        assert_eq!(client.chain_id(), GNOSIS);
    }
}