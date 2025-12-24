use crate::prover::Prover;
use crate::types::{Result, ColibriError, Status, HttpRequest};
use reqwest::Client;
use serde_json;

pub struct ProverClient {
    prover: Prover,
    http_client: Client,
    beacon_api_url: Option<String>,
    eth_rpc_url: Option<String>,
}

impl ProverClient {
    pub fn new(method: &str, params: &str, chain_id: u64, flags: u32) -> Result<Self> {
        let prover = Prover::new(method, params, chain_id, flags)?;
        let http_client = Client::new();

        Ok(Self {
            prover,
            http_client,
            beacon_api_url: None,
            eth_rpc_url: None,
        })
    }

    pub fn with_urls(
        method: &str,
        params: &str,
        chain_id: u64,
        flags: u32,
        beacon_api_url: Option<String>,
        eth_rpc_url: Option<String>,
    ) -> Result<Self> {
        let prover = Prover::new(method, params, chain_id, flags)?;
        let http_client = Client::new();

        Ok(Self {
            prover,
            http_client,
            beacon_api_url,
            eth_rpc_url,
        })
    }

    fn parse_status(&mut self) -> Result<Status> {
        let json_str = self.prover.execute_json_status()?;
        let status: Status = serde_json::from_str(&json_str)?;
        Ok(status)
    }

    async fn handle_request(&self, req: &HttpRequest) -> Result<Vec<u8>> {
        // Build the full URL based on request type
        // The C library returns relative paths, we need to combine with base URLs
        let full_url = match req.request_type.as_str() {
            "beacon_api" | "beacon" => {
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

        // Add headers
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

    pub async fn run_to_completion(&mut self) -> Result<Vec<u8>> {
        loop {
            match self.parse_status()? {
                Status::Pending { requests } => {
                    for request in requests {
                        match self.handle_request(&request).await {
                            Ok(data) => {
                                self.prover.set_response(
                                    request.request_id,
                                    &data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                self.prover.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Success => {
                    return self.prover.get_proof();
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }

    pub async fn run_to_completion_concurrent(&mut self) -> Result<Vec<u8>> {
        loop {
            match self.parse_status()? {
                Status::Pending { requests } => {
                    let futures: Vec<_> = requests
                        .iter()
                        .map(|req| self.handle_request(req))
                        .collect();

                    let results = futures::future::join_all(futures).await;

                    for (request, result) in requests.iter().zip(results.iter()) {
                        match result {
                            Ok(data) => {
                                self.prover.set_response(
                                    request.request_id,
                                    data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                self.prover.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Success => {
                    return self.prover.get_proof();
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }
}