use super::prover::Prover;
use super::verifier::Verifier;
use super::helpers;
use crate::types::{Result, ColibriError, Status, HttpRequest, MethodType, RequestType, HttpMethod, Encoding};
use crate::types::chain::{
    MAINNET, SEPOLIA, GNOSIS, CHIADO,
    MAINNET_ETH_RPC, SEPOLIA_ETH_RPC, GNOSIS_ETH_RPC, CHIADO_ETH_RPC,
    MAINNET_BEACON_API, SEPOLIA_BEACON_API, GNOSIS_BEACON_API, CHIADO_BEACON_API,
    MAINNET_CHECKPOINTZ_1, MAINNET_CHECKPOINTZ_2, MAINNET_CHECKPOINTZ_3, MAINNET_CHECKPOINTZ_4,
    MAINNET_PROVER, SEPOLIA_PROVER, GNOSIS_PROVER, CHIADO_PROVER, DEFAULT_PROVER,
};
use reqwest::Client;
use serde_json;
use std::time::Duration;

/// Configuration for the Colibri client
#[derive(Clone)]
pub struct ClientConfig {
    pub chain_id: u64,
    pub eth_rpcs: Vec<String>,
    pub beacon_apis: Vec<String>,
    pub checkpointz: Vec<String>,
    pub provers: Vec<String>,
    pub trusted_checkpoint: Option<String>,
    pub include_code: bool,
}

impl ClientConfig {
    /// Create a new configuration with default servers for the given chain
    pub fn new(chain_id: u64) -> Self {
        Self {
            chain_id,
            eth_rpcs: default_eth_rpcs(chain_id),
            beacon_apis: default_beacon_apis(chain_id),
            checkpointz: default_checkpointz(chain_id),
            provers: default_provers(chain_id),
            trusted_checkpoint: None,
            include_code: false,
        }
    }

    /// Set custom Ethereum RPC URLs
    pub fn with_eth_rpcs(mut self, urls: Vec<String>) -> Self {
        self.eth_rpcs = urls;
        self
    }

    /// Set custom beacon API URLs
    pub fn with_beacon_apis(mut self, urls: Vec<String>) -> Self {
        self.beacon_apis = urls;
        self
    }

    /// Set custom checkpointz URLs
    pub fn with_checkpointz(mut self, urls: Vec<String>) -> Self {
        self.checkpointz = urls;
        self
    }

    /// Set custom prover URLs
    pub fn with_provers(mut self, urls: Vec<String>) -> Self {
        self.provers = urls;
        self
    }

    /// Set trusted checkpoint
    pub fn with_trusted_checkpoint(mut self, checkpoint: String) -> Self {
        self.trusted_checkpoint = Some(checkpoint);
        self
    }

    /// Set include_code flag for proofs
    pub fn with_include_code(mut self, include: bool) -> Self {
        self.include_code = include;
        self
    }
}

fn default_eth_rpcs(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![MAINNET_ETH_RPC.into()],
        SEPOLIA => vec![SEPOLIA_ETH_RPC.into()],
        GNOSIS => vec![GNOSIS_ETH_RPC.into()],
        CHIADO => vec![CHIADO_ETH_RPC.into()],
        _ => vec![MAINNET_ETH_RPC.into()],
    }
}

fn default_beacon_apis(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![MAINNET_BEACON_API.into()],
        SEPOLIA => vec![SEPOLIA_BEACON_API.into()],
        GNOSIS => vec![GNOSIS_BEACON_API.into()],
        CHIADO => vec![CHIADO_BEACON_API.into()],
        _ => vec![MAINNET_BEACON_API.into()],
    }
}

fn default_checkpointz(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![
            MAINNET_CHECKPOINTZ_1.into(),
            MAINNET_CHECKPOINTZ_2.into(),
            MAINNET_CHECKPOINTZ_3.into(),
            MAINNET_CHECKPOINTZ_4.into(),
        ],
        _ => vec![],
    }
}

fn default_provers(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![MAINNET_PROVER.into()],
        SEPOLIA => vec![SEPOLIA_PROVER.into()],
        GNOSIS => vec![GNOSIS_PROVER.into()],
        CHIADO => vec![CHIADO_PROVER.into()],
        _ => vec![DEFAULT_PROVER.into()],
    }
}

/// Unified client for both proving and verifying operations.
///
/// This client handles the HTTP communication required for proof generation
/// and verification. It manages connections to beacon nodes and Ethereum RPC endpoints
/// with automatic fallback across multiple servers.
///
/// # Example
///
/// ```rust
/// use colibri::{ColibriClient, ClientConfig};
///
/// // Simple: use defaults for mainnet
/// let client = ColibriClient::new(None, None);
///
/// // Sepolia with defaults
/// let client = ColibriClient::new(Some(ClientConfig::new(11155111)), None);
///
/// // Custom configuration
/// let config = ClientConfig::new(1)
///     .with_eth_rpcs(vec!["https://my-rpc.com".into()])
///     .with_beacon_apis(vec!["https://my-beacon.com".into()]);
/// let client = ColibriClient::new(Some(config), None);
/// ```
pub struct ColibriClient {
    http_client: Client,
    config: ClientConfig,
}

impl ColibriClient {
    /// Create a new Colibri client
    ///
    /// # Arguments
    ///
    /// * `config` - Optional client configuration. If None, uses mainnet defaults.
    /// * `storage` - Optional storage implementation. If None, uses file storage.
    ///
    /// # Example
    ///
    /// ```rust
    /// use colibri::{ColibriClient, ClientConfig, MemoryStorage, Storage};
    ///
    /// // Mainnet with defaults
    /// let client = ColibriClient::new(None, None);
    ///
    /// // Sepolia
    /// let client = ColibriClient::new(Some(ClientConfig::new(11155111)), None);
    ///
    /// // Custom config with memory storage
    /// let config = ClientConfig::new(1)
    ///     .with_eth_rpcs(vec!["https://my-rpc.com".into()]);
    /// let storage: Option<Box<dyn Storage>> = Some(MemoryStorage::new());
    /// let client = ColibriClient::new(Some(config), storage);
    /// ```
    pub fn new(
        config: Option<ClientConfig>,
        storage: Option<Box<dyn crate::storage::Storage>>,
    ) -> Self {
        let config = config.unwrap_or_else(|| ClientConfig::new(MAINNET));

        let http_client = Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .unwrap_or_else(|_| Client::new());

        let storage = storage.unwrap_or_else(|| {
            match crate::storage::default_storage() {
                Ok(fs) => Box::new(fs) as Box<dyn crate::storage::Storage>,
                Err(_) => crate::storage::MemoryStorage::new() as Box<dyn crate::storage::Storage>,
            }
        });

        crate::storage::ffi::register_global_storage(storage);

        Self { http_client, config }
    }

    /// Get the chain ID
    pub fn chain_id(&self) -> u64 {
        self.config.chain_id
    }

    /// Check if a method is supported and get its type
    pub fn get_method_support(&self, method: &str) -> Result<MethodType> {
        helpers::get_method_type(self.config.chain_id, method)
    }

    fn get_servers_for_request(&self, request_type: RequestType) -> &[String] {
        match request_type {
            RequestType::Checkpointz => &self.config.checkpointz,
            RequestType::BeaconApi => &self.config.beacon_apis,
            RequestType::JsonRpc => &self.config.eth_rpcs,
        }
    }

    async fn execute_request(
        &self,
        req: &HttpRequest,
        server: &str,
    ) -> Result<Vec<u8>> {
        let full_url = if req.url.is_empty() || req.request_type == RequestType::JsonRpc {
            server.to_string()
        } else {
            let base = server.trim_end_matches('/');
            let path = req.url.trim_start_matches('/');
            format!("{}/{}", base, path)
        };

        let mut request_builder = match req.method {
            HttpMethod::Get => self.http_client.get(&full_url),
            HttpMethod::Post => {
                let mut builder = self.http_client.post(&full_url);

                if req.request_type == RequestType::JsonRpc {
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
                } else if let Some(body) = &req.body {
                    builder = builder.body(body.clone());
                }

                builder
            }
        };

        request_builder = match req.encoding {
            Encoding::Ssz => request_builder.header("Accept", "application/octet-stream"),
            Encoding::Json => request_builder.header("Accept", "application/json"),
        };

        for (key, value) in &req.headers {
            request_builder = request_builder.header(key, value);
        }

        let response = request_builder.send().await?;
        let status = response.status();
        let bytes = response.bytes().await?;

        if !status.is_success() {
            return Err(ColibriError::Ffi(format!(
                "HTTP {} from {}: {}",
                status.as_u16(),
                server,
                String::from_utf8_lossy(&bytes)
            )));
        }

        Ok(bytes.to_vec())
    }

    async fn handle_request(&self, req: &HttpRequest) -> Result<Vec<u8>> {
        let servers = self.get_servers_for_request(req.request_type);

        if servers.is_empty() {
            return Err(ColibriError::Ffi(format!(
                "No servers configured for request type '{:?}'",
                req.request_type
            )));
        }

        let mut last_error = None;

        for (i, server) in servers.iter().enumerate() {
            if req.exclude_mask & (1 << i) != 0 {
                continue;
            }

            match self.execute_request(req, server).await {
                Ok(data) => return Ok(data),
                Err(e) => {
                    last_error = Some(e);
                    continue;
                }
            }
        }

        Err(last_error.unwrap_or_else(|| {
            ColibriError::Ffi(format!("All servers failed for {:?}", req.request_type))
        }))
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
    /// let client = colibri::ColibriClient::new(None, None);
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
    /// * `trusted_checkpoint` - Optional trusted checkpoint (empty string uses client default)
    ///
    /// # Returns
    ///
    /// Returns the verified result as a JSON value.
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// # async fn example() -> colibri::Result<()> {
    /// let client = colibri::ColibriClient::new(None, None);
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
        let checkpoint = if trusted_checkpoint.is_empty() {
            self.config.trusted_checkpoint.as_deref().unwrap_or("")
        } else {
            trusted_checkpoint
        };

        let mut verifier = Verifier::new(proof, method, params, chain_id, checkpoint)?;

        loop {
            let json_str = verifier.execute_json_status()?;
            let status_json: serde_json::Value = serde_json::from_str(&json_str)?;
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

    #[test]
    fn test_default_configs() {
        let config = ClientConfig::new(MAINNET);
        assert!(!config.eth_rpcs.is_empty());
        assert!(!config.beacon_apis.is_empty());
        assert!(!config.provers.is_empty());
    }

    #[test]
    fn test_client_defaults() {
        let client = ColibriClient::new(None, None);
        assert_eq!(client.chain_id(), 1);
        assert!(!client.config.eth_rpcs.is_empty());
    }

    #[test]
    fn test_client_with_config() {
        let config = ClientConfig::new(MAINNET)
            .with_beacon_apis(vec!["https://beacon.test".into()])
            .with_eth_rpcs(vec!["https://rpc.test".into()]);
        let client = ColibriClient::new(Some(config), None);
        assert_eq!(client.config.beacon_apis, vec!["https://beacon.test"]);
        assert_eq!(client.config.eth_rpcs, vec!["https://rpc.test"]);
    }

    #[test]
    fn test_config_builder() {
        let config = ClientConfig::new(MAINNET)
            .with_eth_rpcs(vec!["https://custom-rpc.com".into()])
            .with_include_code(true);

        assert_eq!(config.eth_rpcs, vec!["https://custom-rpc.com"]);
        assert!(config.include_code);
    }

    #[test]
    fn test_get_servers_for_request() {
        let client = ColibriClient::new(None, None);

        let beacon_servers = client.get_servers_for_request(RequestType::BeaconApi);
        assert!(!beacon_servers.is_empty());

        let rpc_servers = client.get_servers_for_request(RequestType::JsonRpc);
        assert!(!rpc_servers.is_empty());
    }

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
                    method: HttpMethod::Get,
                    headers: HashMap::new(),
                    body: None,
                    payload: None,
                    encoding: Encoding::Json,
                    request_type: RequestType::BeaconApi,
                    chain_id: MAINNET,
                    exclude_mask: 0,
                },
            }
        }

        fn with_url(mut self, url: &str) -> Self {
            self.request.url = url.to_string();
            self
        }

        fn with_type(mut self, request_type: RequestType) -> Self {
            self.request.request_type = request_type;
            self
        }

        fn build(self) -> HttpRequest {
            self.request
        }
    }

    #[tokio::test]
    async fn test_handle_request_with_fallback() {
        let config = ClientConfig::new(1)
            .with_beacon_apis(vec![
                "https://invalid-server-1.test".into(),
                "https://invalid-server-2.test".into(),
            ]);

        let client = ColibriClient::new(Some(config), None);

        let request = TestRequestBuilder::new()
            .with_url("/eth/v1/beacon/genesis")
            .with_type(RequestType::BeaconApi)
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_handle_request_empty_servers() {
        let config = ClientConfig::new(1)
            .with_checkpointz(vec![]);

        let client = ColibriClient::new(Some(config), None);

        let request = TestRequestBuilder::new()
            .with_type(RequestType::Checkpointz)
            .build();

        let result = client.handle_request(&request).await;
        assert!(result.is_err());

        if let Err(e) = result {
            assert!(e.to_string().contains("No servers configured"));
        }
    }
}
