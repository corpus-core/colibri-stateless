use crate::prover::Prover;
use crate::verifier::Verifier;
use crate::types::{Result, ColibriError, Status, HttpRequest};
use reqwest::Client;
use serde_json;

/// Unified client for both proving and verifying operations
pub struct ColibriClient {
    http_client: Client,
    beacon_api_url: Option<String>,
    eth_rpc_url: Option<String>,
}

impl ColibriClient {
    /// Create a new client without URLs (must be set later or use defaults)
    pub fn new() -> Self {
        Self {
            http_client: Client::new(),
            beacon_api_url: None,
            eth_rpc_url: None,
        }
    }

    /// Create a new client with specified URLs
    pub fn with_urls(
        beacon_api_url: Option<String>,
        eth_rpc_url: Option<String>,
    ) -> Self {
        Self {
            http_client: Client::new(),
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

    /// Generate a proof for an Ethereum RPC method
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

    /// Verify a proof and return the verified result
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
                                crate::helpers::set_request_response(
                                    request.request_id as u64,
                                    &data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                // For verifier, we need to handle errors differently
                                // since Verifier doesn't have set_error method exposed
                                return Err(ColibriError::Ffi(format!(
                                    "Request failed: {}",
                                    err
                                )));
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
