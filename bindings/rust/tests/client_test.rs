use colibri::{ColibriClient, ColibriError, Result};

#[cfg(test)]
mod client_operations {
    use super::*;

    #[tokio::test]
    async fn test_prove_without_urls_should_fail() {
        let client = ColibriClient::new(None, None, None);

        let result = client.prove("eth_blockNumber", "[]", 1, 0).await;

        assert!(result.is_err(), "Should fail without configured URLs");

        if let Err(e) = result {
            let error_msg = e.to_string();
            assert!(
                error_msg.contains("not configured") || error_msg.contains("URL"),
                "Error should mention missing URL configuration: {}",
                error_msg
            );
        }
    }

    #[tokio::test]
    async fn test_verify_without_urls_should_fail() {
        let client = ColibriClient::new(None, None, None);
        let fake_proof = vec![0u8; 100];

        let result = client.verify(&fake_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err(), "Should fail without configured URLs");
    }

    #[tokio::test]
    async fn test_prove_with_invalid_method() {
        let client = ColibriClient::new(
            Some("https://beacon.example.com".to_string()),
            Some("https://rpc.example.com".to_string()),
            None,
        );

        let result = client.prove("invalid_method", "[]", 1, 0).await;

        assert!(result.is_err(), "Should fail with invalid method");
    }

    #[tokio::test]
    async fn test_verify_with_invalid_proof() {
        let client = ColibriClient::new(
            Some("https://beacon.example.com".to_string()),
            Some("https://rpc.example.com".to_string()),
            None,
        );

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
        let client = ColibriClient::new(None, None, None);

        let result = client.prove("eth_blockNumber", "[]", 1, 0).await;

        assert!(result.is_err());
        match result {
            Err(ColibriError::Ffi(msg)) => {
                assert!(msg.contains("not configured") || msg.contains("URL"));
            }
            _ => panic!("Expected FFI error for missing URL"),
        }
    }

    #[tokio::test]
    async fn test_error_for_missing_rpc_url() {
        let client = ColibriClient::new(
            Some("https://beacon.test".to_string()),
            None,
            None,
        );

        // eth_getBalance would need an RPC URL
        let result = client.prove("eth_getBalance", "[\"0x0\", \"latest\"]", 1, 0).await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_invalid_json_params() {
        let client = ColibriClient::new(
            Some("https://beacon.test".to_string()),
            Some("https://rpc.test".to_string()),
            None,
        );

        // Invalid JSON in params
        let result = client.prove("eth_blockNumber", "[invalid json", 1, 0).await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_empty_proof() {
        let client = ColibriClient::new(
            Some("https://beacon.test".to_string()),
            Some("https://rpc.test".to_string()),
            None,
        );

        let empty_proof = vec![];

        let result = client.verify(&empty_proof, "eth_blockNumber", "[]", 1, "").await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_error_for_malformed_proof() {
        let client = ColibriClient::new(
            Some("https://beacon.test".to_string()),
            Some("https://rpc.test".to_string()),
            None,
        );

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
        let client = ColibriClient::new(
            Some("https://lodestar-mainnet.chainsafe.io".to_string()),
            Some("https://ethereum-rpc.publicnode.com".to_string()),
            None,
        );
        assert!(std::mem::size_of_val(&client) > 0);
    }

    #[test]
    fn test_sepolia_configuration() {
        let client = ColibriClient::new(
            Some("https://lodestar-sepolia.chainsafe.io".to_string()),
            Some("https://sepolia.gateway.tenderly.co".to_string()),
            None,
        );
        assert!(std::mem::size_of_val(&client) > 0);
    }

    #[test]
    fn test_gnosis_configuration() {
        let client = ColibriClient::new(
            Some("https://beacon.gnosischain.com".to_string()),
            Some("https://rpc.gnosischain.com".to_string()),
            None,
        );
        assert!(std::mem::size_of_val(&client) > 0);
    }
}