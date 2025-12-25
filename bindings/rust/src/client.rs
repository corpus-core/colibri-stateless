use crate::prover::Prover;
use crate::verifier::Verifier;
use crate::types::{Result, ColibriError, Status, HttpRequest, MethodType};
use crate::helpers;
use reqwest::Client;
use serde_json;
use std::time::Duration;

/// Unified client for both proving and verifying operations.
///
/// This client handles the HTTP communication required for proof generation
/// and verification. It manages connections to beacon nodes and Ethereum RPC endpoints.
///
/// # Example
///
/// ```rust
/// use colibri::ColibriClient;
///
/// let client = ColibriClient::with_urls(
///     Some("https://beacon-node.example.com".to_string()),
///     Some("https://eth-rpc.example.com".to_string()),
/// );
/// ```
pub struct ColibriClient {
    http_client: Client,
    beacon_api_url: Option<String>,
    eth_rpc_url: Option<String>,
}

impl ColibriClient {
    /// Create a new client without URLs
    pub fn new() -> Self {
        let http_client = Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .unwrap_or_else(|_| Client::new());

        Self {
            http_client,
            beacon_api_url: None,
            eth_rpc_url: None,
        }
    }

    /// Create a new client with specified URLs
    pub fn with_urls(
        beacon_api_url: Option<String>,
        eth_rpc_url: Option<String>,
    ) -> Self {
        let http_client = Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .unwrap_or_else(|_| Client::new());

        Self {
            http_client,
            beacon_api_url,
            eth_rpc_url,
        }
    }

    /// Set the beacon API URL
    pub fn set_beacon_api_url(&mut self, url: String) {
        self.beacon_api_url = Some(url);
    }

    /// Set the Ethereum RPC URL
    pub fn set_eth_rpc_url(&mut self, url: String) {
        self.eth_rpc_url = Some(url);
    }

    /// Check if a method is supported and get its type
    pub fn get_method_support(&self, method: &str, chain_id: u64) -> Result<MethodType> {
        helpers::get_method_type(chain_id, method)
    }

    async fn handle_request(&self, req: &HttpRequest) -> Result<Vec<u8>> {
        // Build the full URL based on request type
        // The C library returns relative paths, we need to combine with base URLs
        let full_url = match req.request_type.as_str() {
            "beacon_api" | "beacon" | "checkpointz" => {
                match &self.beacon_api_url {
                    Some(base) => {
                        let base = base.trim_end_matches('/');
                        let path = req.url.trim_start_matches('/');
                        format!("{}/{}", base, path)
                    }
                    None => {
                        return Err(ColibriError::Ffi(
                            "Beacon API URL not configured. Set beacon_api_url when creating the client.".to_string()
                        ));
                    }
                }
            }
            "json_rpc" | "eth_rpc" => {
                // For JSON-RPC/ETH-RPC, the URL should be the endpoint itself
                // The request URL is empty for RPC calls
                match &self.eth_rpc_url {
                    Some(url) => url.clone(),
                    None => {
                        return Err(ColibriError::Ffi(
                            "Ethereum RPC URL not configured. Set eth_rpc_url when creating the client.".to_string()
                        ));
                    }
                }
            }
            _ => {
                // For other types, check if it's already a full URL
                if req.url.starts_with("http://") || req.url.starts_with("https://") {
                    req.url.clone()
                } else {
                    return Err(ColibriError::Ffi(
                        format!("Unknown request type '{}' with relative URL '{}'", req.request_type, req.url)
                    ));
                }
            }
        };

        let mut request_builder = match req.method.to_uppercase().as_str() {
            "GET" => self.http_client.get(&full_url),
            "POST" => {
                let mut builder = self.http_client.post(&full_url);

                // For RPC requests, we need to send JSON payload
                if req.request_type == "eth_rpc" || req.request_type == "json_rpc" {
                    if let Some(payload) = &req.payload {
                        let json_body = serde_json::to_string(payload)?;
                        builder = builder
                            .header("Content-Type", "application/json")
                            .body(json_body);
                    } else if let Some(body) = &req.body {
                        builder = builder
                            .header("Content-Type", "application/json")
                            .body(body.clone());
                    }
                } else {
                    if let Some(body) = &req.body {
                        builder = builder.body(body.clone());
                    }
                }

                builder
            }
            method => {
                return Err(ColibriError::Ffi(format!("Unsupported HTTP method: {}", method)));
            }
        };

        // Add Accept header based on encoding
        if req.encoding == "ssz" {
            request_builder = request_builder.header("Accept", "application/octet-stream");
        } else {
            request_builder = request_builder.header("Accept", "application/json");
        }

        // Add any additional headers from the request
        for (key, value) in &req.headers {
            request_builder = request_builder.header(key, value);
        }

        // Send request
        let response = request_builder.send().await?;
        let status = response.status();
        let bytes = response.bytes().await?;

        if !status.is_success() {
            return Err(ColibriError::Ffi(format!(
                "HTTP {} error for {}: {}",
                status.as_u16(), req.request_type,
                String::from_utf8_lossy(&bytes)
            )));
        }

        Ok(bytes.to_vec())
    }

    /// Generate a proof for an Ethereum RPC method.
    ///
    /// # Arguments
    ///
    /// * `method` - The RPC method name (e.g., "eth_blockNumber", "eth_getBalance")
    /// * `params` - JSON-encoded parameters for the method
    /// * `chain_id` - The chain ID (1 for mainnet, 11155111 for Sepolia, etc.)
    /// * `flags` - Optional flags for proof generation (usually 0)
    ///
    /// # Returns
    ///
    /// Returns the generated proof as a byte vector.
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// # async fn example() -> colibri::Result<()> {
    /// let client = colibri::ColibriClient::new();
    /// let proof = client.prove("eth_blockNumber", "[]", 1, 0).await?;
    /// # Ok(())
    /// # }
    /// ```
    pub async fn prove(
        &self,
        method: &str,
        params: &str,
        chain_id: u64,
        flags: u32,
    ) -> Result<Vec<u8>> {
        let mut prover = Prover::new(method, params, chain_id, flags)?;

        loop {
            let json_str = prover.execute_json_status()?;
            let status: Status = serde_json::from_str(&json_str)?;

            match status {
                Status::Pending { requests } => {
                    for request in requests {
                        match self.handle_request(&request).await {
                            Ok(data) => {
                                prover.set_response(
                                    request.request_id,
                                    &data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                prover.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Success => {
                    return prover.get_proof();
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }

    /// Verify a proof and return the verified result.
    ///
    /// # Arguments
    ///
    /// * `proof` - The proof bytes to verify
    /// * `method` - The RPC method name that was proved
    /// * `params` - The parameters that were used for proving
    /// * `chain_id` - The chain ID
    /// * `trusted_checkpoint` - Optional trusted checkpoint (usually empty string)
    ///
    /// # Returns
    ///
    /// Returns the verified result as a JSON value.
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// # async fn example() -> colibri::Result<()> {
    /// let client = colibri::ColibriClient::new();
    /// let proof = vec![/* proof bytes */];
    /// let result = client.verify(&proof, "eth_blockNumber", "[]", 1, "").await?;
    /// # Ok(())
    /// # }
    /// ```
    pub async fn verify(
        &self,
        proof: &[u8],
        method: &str,
        params: &str,
        chain_id: u64,
        trusted_checkpoint: &str,
    ) -> Result<serde_json::Value> {
        let mut verifier = Verifier::new(proof, method, params, chain_id, trusted_checkpoint)?;

        loop {
            let json_str = verifier.execute_json_status()?;
            let status_json: serde_json::Value = serde_json::from_str(&json_str)?;

            // Parse status separately for matching
            let status: Status = serde_json::from_value(status_json.clone())?;

            match status {
                Status::Pending { requests } => {
                    for request in requests {
                        match self.handle_request(&request).await {
                            Ok(data) => {
                                verifier.set_response(
                                    request.request_id,
                                    &data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                verifier.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Success => {
                    // Extract the result from the success status
                    if let Some(result) = status_json.get("result") {
                        return Ok(result.clone());
                    }
                    return Err(ColibriError::Ffi("Success but no result found".to_string()));
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    struct TestRequestBuilder {
        request: HttpRequest,
    }

    impl TestRequestBuilder {
        fn new() -> Self {
            Self {
                request: HttpRequest {
                    request_id: 1,
                    node_index: 0,
                    url: String::new(),
                    method: "GET".to_string(),
                    headers: HashMap::new(),
                    body: None,
                    payload: None,
                    encoding: "json".to_string(),
                    request_type: "beacon_api".to_string(),
                    chain_id: 1,
                    exclude_mask: String::new(),
                },
            }
        }

        fn with_url(mut self, url: &str) -> Self {
            self.request.url = url.to_string();
            self
        }

        fn with_type(mut self, request_type: &str) -> Self {
            self.request.request_type = request_type.to_string();
            self
        }

        fn with_encoding(mut self, encoding: &str) -> Self {
            self.request.encoding = encoding.to_string();
            self
        }

        fn build(self) -> HttpRequest {
            self.request
        }
    }

    #[test]
    fn test_client_creation() {
        let client = ColibriClient::new();
        assert!(std::mem::size_of_val(&client) > 0);

        let client_with_urls = ColibriClient::with_urls(
            Some("https://beacon.test".to_string()),
            Some("https://rpc.test".to_string()),
        );
        assert!(std::mem::size_of_val(&client_with_urls) > 0);
    }

    #[test]
    fn test_client_url_setters() {
        let mut client = ColibriClient::new();
        client.set_beacon_api_url("https://beacon.test".to_string());
        client.set_eth_rpc_url("https://rpc.test".to_string());
        assert_eq!(client.beacon_api_url, Some("https://beacon.test".to_string()));
        assert_eq!(client.eth_rpc_url, Some("https://rpc.test".to_string()));
    }

    #[tokio::test]
    async fn test_handle_request_beacon_api() {
        let client = ColibriClient::with_urls(
            Some("https://beacon.test".to_string()),
            None,
        );

        let request = TestRequestBuilder::new()
            .with_url("/eth/v1/beacon/genesis")
            .with_type("beacon_api")
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_handle_request_missing_beacon_url() {
        let client = ColibriClient::new();

        let request = TestRequestBuilder::new()
            .with_url("/eth/v1/beacon/genesis")
            .with_type("beacon_api")
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());

        if let Err(e) = result {
            let error_msg = e.to_string();
            assert!(error_msg.contains("Beacon API URL not configured"));
        }
    }

    #[tokio::test]
    async fn test_handle_request_missing_rpc_url() {
        let client = ColibriClient::new();

        let request = TestRequestBuilder::new()
            .with_url("")
            .with_type("eth_rpc")
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());

        if let Err(e) = result {
            let error_msg = e.to_string();
            assert!(error_msg.contains("Ethereum RPC URL not configured"));
        }
    }

    #[tokio::test]
    async fn test_handle_request_checkpointz_type() {
        let client = ColibriClient::with_urls(
            Some("https://checkpoint.test".to_string()),
            None,
        );

        let request = TestRequestBuilder::new()
            .with_url("/eth/v1/beacon/light_client/bootstrap/0x123")
            .with_type("checkpointz")
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_handle_request_ssz_encoding() {
        let client = ColibriClient::with_urls(
            Some("https://beacon.test".to_string()),
            None,
        );

        let request = TestRequestBuilder::new()
            .with_url("/eth/v1/beacon/blocks/head")
            .with_type("beacon_api")
            .with_encoding("ssz")
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());
    }
}
