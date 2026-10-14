//! High-level [`Colibri`] host: manages HTTP + storage + freshness and
//! drives the C state machine to completion.
//!
//! Mirrors [`bindings/python/src/colibri/client.py`][py] one to one --
//! same URL routing, same flag encoding, same `rpc()` /
//! `create_proof()` / `verify_proof()` surface. Every request loop is
//! entirely asynchronous and fetches pending data in parallel via
//! `futures::future::join_all`.
//!
//! [py]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/python/src/colibri/client.py

use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use async_trait::async_trait;
use futures::future::join_all;
#[cfg(not(target_family = "wasm"))]
use reqwest::Client as HttpClient;
use serde_json::Value as JsonValue;

#[cfg(not(target_family = "wasm"))]
use super::fetch::FetchRequest;
use super::fetch::{DynFetcher, FetchRequestHandler};
use super::prover::Prover;
use super::request::{set_error as req_set_error, set_response as req_set_response};
use super::rpc::{set_checkpoint, RpcCtx};
use super::verifier::Verifier;
#[cfg(not(target_family = "wasm"))]
use crate::storage::FileStorage;
use crate::storage::{register_storage, MemoryStorage, Storage};
#[cfg(not(target_family = "wasm"))]
use crate::types::HttpMethod;
use crate::types::{
    default_beacon_apis, default_checkpointz, default_eth_rpcs, default_provers, ColibriError,
    DataRequest, PrivacyMode, ProverMode, RevertError, RpcError, Status, MAINNET,
};

/// Configuration for a [`Colibri`] client -- endpoint lists, prover
/// flags and freshness policy. Construct via [`Colibri::builder`].
#[derive(Clone)]
#[allow(missing_docs)]
pub struct ColibriConfig {
    pub chain_id: u64,
    pub provers: Vec<String>,
    pub eth_rpcs: Vec<String>,
    pub beacon_apis: Vec<String>,
    pub checkpointz: Vec<String>,
    pub oblivious_nodes: Vec<String>,
    pub trusted_checkpoint: Option<String>,
    pub include_code: bool,
    pub use_accesslist: bool,
    pub zk_proof: bool,
    pub privacy_mode: PrivacyMode,
    pub prover_mode: Option<ProverMode>,
    pub checkpoint_witness_keys: Option<String>,
    pub skip_wsp_check: bool,
    pub logs_completeness: bool,
    /// Upper bound (in seconds) on the age of a `"latest"`-anchored
    /// proof accepted by the verifier. `0` disables the check.
    pub max_latest_age_seconds: u64,
    /// Overall HTTP request timeout.
    pub request_timeout: Duration,
}

impl ColibriConfig {
    /// Fresh configuration with default endpoints for `chain_id`.
    pub fn new(chain_id: u64) -> Self {
        Self {
            chain_id,
            provers: default_provers(chain_id),
            eth_rpcs: default_eth_rpcs(chain_id),
            beacon_apis: default_beacon_apis(chain_id),
            checkpointz: default_checkpointz(chain_id),
            oblivious_nodes: Vec::new(),
            trusted_checkpoint: None,
            include_code: false,
            use_accesslist: true,
            zk_proof: false,
            privacy_mode: PrivacyMode::None,
            prover_mode: None,
            checkpoint_witness_keys: None,
            skip_wsp_check: false,
            logs_completeness: false,
            max_latest_age_seconds: 60,
            request_timeout: Duration::from_secs(30),
        }
    }

    /// Compute the prover flag bitset from user-facing options. Kept in
    /// sync with `Colibri._get_verify_flags`/`Colibri._get_prover_flags`
    /// in the Python binding.
    fn prover_flags(&self) -> u32 {
        let mut flags: u32 = 0;
        if self.include_code {
            flags |= 1;
        }
        if !self.use_accesslist {
            flags |= 1 << 6;
        }
        if self.zk_proof {
            flags |= 1 << 7;
        }
        if self.logs_completeness {
            flags |= 1 << 12;
        }
        flags
    }

    fn verify_flags(&self) -> u32 {
        let pap = self.privacy_mode == PrivacyMode::Basic || !self.oblivious_nodes.is_empty();
        let mut flags: u32 = 0;
        if pap {
            flags |= 2;
        }
        if !self.oblivious_nodes.is_empty() {
            flags |= 1 << 6;
        }
        if self.skip_wsp_check {
            flags |= 1 << 7;
        }
        if self.logs_completeness {
            flags |= 1 << 9;
        }
        flags
    }

    fn min_latest_block_ts(&self) -> u64 {
        if self.max_latest_age_seconds == 0 {
            return 0;
        }
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        now.saturating_sub(self.max_latest_age_seconds)
    }
}

/// Trait for injecting a custom request handler (used by the testing
/// module to serve fixtures from disk). Implementors return the
/// response payload as bytes -- errors are turned into
/// `c4_req_set_error` calls.
///
/// Wasm hosts that only need to supply an HTTP transport (and want
/// Colibri's endpoint routing) should prefer
/// [`FetchRequestHandler`] / [`ColibriBuilder::http_fetch`] over a
/// full custom implementation.
#[async_trait]
pub trait RequestHandler: Send + Sync {
    /// Serve the given request. Returning `Err` triggers
    /// `c4_req_set_error(req_ptr, ...)`.
    async fn handle(&self, request: &DataRequest) -> Result<Vec<u8>, ColibriError>;

    /// Like [`handle`], with the host's Beacon-API → prover fallback
    /// hint (`true` for [`Colibri::rpc`] / [`Colibri::verify_proof`],
    /// `false` for [`Colibri::create_proof`]).
    ///
    /// Returns `(body, node_index)` so the C core can set
    /// `exclude_mask` on retry (`1 << node_index`). The default
    /// forwards to [`handle`] and reports `node_index = 0`.
    /// [`FetchRequestHandler`] returns the index of the server that
    /// succeeded.
    async fn handle_with_fallback(
        &self,
        request: &DataRequest,
        _use_prover_fallback: bool,
    ) -> Result<(Vec<u8>, u16), ColibriError> {
        self.handle(request).await.map(|bytes| (bytes, 0))
    }
}

/// Fluent builder for [`Colibri`].
pub struct ColibriBuilder {
    config: ColibriConfig,
    storage: Option<Box<dyn Storage>>,
    request_handler: Option<Arc<dyn RequestHandler>>,
    http_fetch: Option<DynFetcher>,
    #[cfg(not(target_family = "wasm"))]
    http_client: Option<HttpClient>,
}

impl ColibriBuilder {
    fn new(chain_id: u64) -> Self {
        Self {
            config: ColibriConfig::new(chain_id),
            storage: None,
            request_handler: None,
            http_fetch: None,
            #[cfg(not(target_family = "wasm"))]
            http_client: None,
        }
    }

    /// Colibri prover endpoints (used for `prover` requests + remote
    /// / hybrid proof modes).
    pub fn provers(mut self, urls: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.config.provers = urls.into_iter().map(Into::into).collect();
        self
    }
    /// Execution-layer JSON-RPC endpoints.
    pub fn eth_rpcs(mut self, urls: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.config.eth_rpcs = urls.into_iter().map(Into::into).collect();
        self
    }
    /// Beacon-chain REST endpoints.
    pub fn beacon_apis(mut self, urls: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.config.beacon_apis = urls.into_iter().map(Into::into).collect();
        self
    }
    /// Beacon checkpoint sync endpoints.
    pub fn checkpointz(mut self, urls: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.config.checkpointz = urls.into_iter().map(Into::into).collect();
        self
    }
    /// Oblivious-node endpoints for privacy-preserving
    /// `eth_getProof` (PAP).
    pub fn oblivious_nodes(mut self, urls: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.config.oblivious_nodes = urls.into_iter().map(Into::into).collect();
        self
    }
    /// Set the trusted checkpoint (66-char `0x`-prefixed hex).
    pub fn trusted_checkpoint(mut self, checkpoint: impl Into<String>) -> Self {
        self.config.trusted_checkpoint = Some(checkpoint.into());
        self
    }
    /// Include contract code in state proofs (prover flag 1).
    pub fn include_code(mut self, include: bool) -> Self {
        self.config.include_code = include;
        self
    }
    /// Use `eth_createAccessList` for `eth_call` proofs (default). Set
    /// to `false` to fall back to `debug_traceCall`.
    pub fn use_accesslist(mut self, use_accesslist: bool) -> Self {
        self.config.use_accesslist = use_accesslist;
        self
    }
    /// Request ZK-based sync proofs from a remote prover.
    pub fn zk_proof(mut self, zk: bool) -> Self {
        self.config.zk_proof = zk;
        self
    }
    /// Enable PAP (Pragmatic Adaptive Privacy) mode.
    pub fn privacy_mode(mut self, mode: PrivacyMode) -> Self {
        self.config.privacy_mode = mode;
        self
    }
    /// Force a specific [`ProverMode`]. When unset, the mode is
    /// derived from whether any prover URLs are configured.
    pub fn prover_mode(mut self, mode: ProverMode) -> Self {
        self.config.prover_mode = Some(mode);
        self
    }
    /// Witness signer keys (hex, `0x`-prefixed) for accepting
    /// alternative trust anchors.
    pub fn checkpoint_witness_keys(mut self, keys: impl Into<String>) -> Self {
        self.config.checkpoint_witness_keys = Some(keys.into());
        self
    }
    /// Skip the Weak Subjectivity Period check. SECURITY: only safe
    /// with an alternative trust anchor (witness signatures /
    /// hard-coded checkpoint).
    pub fn skip_wsp_check(mut self, skip: bool) -> Self {
        self.config.skip_wsp_check = skip;
        self
    }
    /// Enable log-completeness proofs for `eth_getLogs`.
    pub fn logs_completeness(mut self, enable: bool) -> Self {
        self.config.logs_completeness = enable;
        self
    }
    /// Maximum age (in seconds) accepted for a `"latest"`-anchored
    /// proof. `0` disables the freshness check.
    pub fn max_latest_age_seconds(mut self, seconds: u64) -> Self {
        self.config.max_latest_age_seconds = seconds;
        self
    }
    /// Overall HTTP request timeout applied to the default reqwest
    /// client (ignored when a custom [`http_client`] is provided).
    ///
    /// [`http_client`]: ColibriBuilder::http_client
    pub fn request_timeout(mut self, timeout: Duration) -> Self {
        self.config.request_timeout = timeout;
        self
    }
    /// Provide a custom storage backend. Registered globally on
    /// [`build`](ColibriBuilder::build) -- the C core only supports
    /// one storage plugin.
    pub fn storage(mut self, storage: impl Storage + 'static) -> Self {
        self.storage = Some(Box::new(storage));
        self
    }
    /// Inject a mock request handler (used by
    /// `bindings/rust/src/testing.rs`). A custom handler **wins** over
    /// [`http_fetch`](Self::http_fetch) and over the native `reqwest`
    /// transport.
    pub fn request_handler(mut self, handler: Arc<dyn RequestHandler>) -> Self {
        self.request_handler = Some(handler);
        self
    }

    /// Supply only the HTTP byte transport. Colibri keeps endpoint
    /// selection, URL joining, `exclude_mask` retries and header
    /// policy -- `fetch` is invoked once per server attempt with a
    /// fully prepared [`super::FetchRequest`].
    ///
    /// This is the default-shaped handler for wasm hosts (WASI has no
    /// sockets). On native it overrides the built-in `reqwest`
    /// client. A full [`request_handler`](Self::request_handler) still
    /// takes precedence when both are set.
    ///
    /// The closure owns the HTTP round-trip:
    ///
    /// - Return `Ok(body)` only for a successful (2xx) response.
    ///   4xx/5xx must be `Err`, otherwise Colibri treats an error
    ///   page as proof / RPC payload.
    /// - Timeouts, TLS and redirects are the host's job;
    ///   [`ColibriConfig::request_timeout`] is **not** applied.
    ///
    /// ```ignore
    /// Colibri::builder(1)
    ///     .provers(["https://mainnet.colibri-proof.tech"])
    ///     .http_fetch(|req| async move {
    ///         host_http(req.method.as_str(), &req.url, &req.headers(), req.body).await
    ///     })
    ///     .build();
    /// ```
    pub fn http_fetch<F, Fut>(mut self, fetch: F) -> Self
    where
        F: Fn(super::FetchRequest) -> Fut + Send + Sync + 'static,
        Fut: std::future::Future<Output = Result<Vec<u8>, ColibriError>> + Send + 'static,
    {
        self.http_fetch = Some(Arc::new(move |req| Box::pin(fetch(req))));
        self
    }
    /// Provide a pre-configured `reqwest` client (e.g. with custom TLS,
    /// proxies, or connection pooling). When omitted, a plain client
    /// with the configured `request_timeout` is used.
    ///
    /// Not available on `wasm*` targets: `reqwest` (hyper / rustls)
    /// cannot be compiled for wasm, so wasm hosts must supply
    /// [`http_fetch`](ColibriBuilder::http_fetch) or a
    /// [`RequestHandler`] via
    /// [`request_handler`](ColibriBuilder::request_handler).
    #[cfg(not(target_family = "wasm"))]
    pub fn http_client(mut self, client: HttpClient) -> Self {
        self.http_client = Some(client);
        self
    }

    /// Consume the builder and return the [`Colibri`].
    pub fn build(self) -> Colibri {
        #[cfg(not(target_family = "wasm"))]
        let http = self.http_client.unwrap_or_else(|| {
            HttpClient::builder()
                .timeout(self.config.request_timeout)
                .build()
                .unwrap_or_else(|_| HttpClient::new())
        });

        let storage: Box<dyn Storage> = match self.storage {
            Some(s) => s,
            None => {
                // WASI Preview 1 has no guaranteed filesystem; skip the
                // FileStorage probe and go straight to in-memory.
                #[cfg(target_family = "wasm")]
                {
                    Box::new(MemoryStorage::new())
                }
                #[cfg(not(target_family = "wasm"))]
                {
                    match FileStorage::new(None) {
                        Ok(fs) => Box::new(fs),
                        Err(_) => Box::new(MemoryStorage::new()),
                    }
                }
            }
        };
        register_boxed_storage(storage);

        let request_handler = match (self.request_handler, self.http_fetch) {
            (Some(handler), _) => Some(handler),
            (None, Some(fetch)) => Some(Arc::new(FetchRequestHandler::from_fetcher(
                self.config.clone(),
                fetch,
            )) as Arc<dyn RequestHandler>),
            (None, None) => None,
        };

        Colibri {
            config: self.config,
            #[cfg(not(target_family = "wasm"))]
            http,
            request_handler,
        }
    }
}

fn register_boxed_storage(storage: Box<dyn Storage>) {
    struct BoxedWrapper(Box<dyn Storage>);
    impl Storage for BoxedWrapper {
        fn get(&self, key: &str) -> Option<Vec<u8>> {
            self.0.get(key)
        }
        fn set(&self, key: &str, value: &[u8]) {
            self.0.set(key, value);
        }
        fn delete(&self, key: &str) {
            self.0.delete(key);
        }
    }
    register_storage(BoxedWrapper(storage));
}

/// High-level Colibri client. Cheap to `clone` once constructed
/// (backed by an `Arc<HttpClient>` on native, and just the shared
/// `Arc<dyn RequestHandler>` on wasm targets).
#[derive(Clone)]
pub struct Colibri {
    config: ColibriConfig,
    #[cfg(not(target_family = "wasm"))]
    http: HttpClient,
    request_handler: Option<Arc<dyn RequestHandler>>,
}

impl Colibri {
    /// Start a builder configured with default endpoints for
    /// `chain_id`. See [`ColibriBuilder`].
    pub fn builder(chain_id: u64) -> ColibriBuilder {
        ColibriBuilder::new(chain_id)
    }

    /// Convenience: default mainnet client.
    pub fn mainnet() -> Self {
        Self::builder(MAINNET).build()
    }

    /// Access the underlying configuration.
    pub fn config(&self) -> &ColibriConfig {
        &self.config
    }

    // ------------------------------------------------------------------
    // High-level API: rpc / create_proof / verify_proof.
    // ------------------------------------------------------------------

    /// Execute an RPC call through the unified state machine (the
    /// recommended entry point). Depending on
    /// [`ColibriConfig::prover_mode`] this runs proof generation and
    /// verification locally, remotely, or in a hybrid mode.
    ///
    /// Returns the verified result as raw JSON. Reverts produce
    /// [`ColibriError::Revert`].
    pub async fn rpc(&self, method: &str, params: &JsonValue) -> Result<JsonValue, ColibriError> {
        let params_json = serde_json::to_string(params)?;
        let resolved_mode = self
            .config
            .prover_mode
            .unwrap_or(if self.config.provers.is_empty() {
                ProverMode::Local
            } else {
                ProverMode::Remote
            });

        let mut ctx = RpcCtx::new(
            method,
            &params_json,
            self.config.chain_id,
            self.config.prover_flags(),
            self.config.verify_flags(),
            resolved_mode,
        )?;

        if resolved_mode == ProverMode::Proxy {
            ctx.set_proxy_urls(
                &self.config.eth_rpcs.join(","),
                &self.config.beacon_apis.join(","),
            )?;
        }
        if let Some(cp) = self.config.trusted_checkpoint.as_deref() {
            set_checkpoint(self.config.chain_id, cp)?;
        }
        if let Some(keys) = self.config.checkpoint_witness_keys.as_deref() {
            ctx.set_witness_keys(keys)?;
        }
        ctx.set_min_latest_block_ts(self.config.min_latest_block_ts());

        loop {
            let raw = ctx.execute_json_status()?;
            match Status::parse(&raw)? {
                Status::Pending { requests } => {
                    self.handle_requests(&requests, true).await;
                }
                Status::Success { result } => return Ok(result.unwrap_or(JsonValue::Null)),
                Status::Revert { data } => return Err(RevertError { data }.into()),
                Status::Error { message } => {
                    return Err(RpcError::new(message).into());
                }
            }
        }
    }

    /// Generate a proof for `method(params)` and return the raw proof
    /// bytes. Skips verification -- use [`verify_proof`] afterwards.
    pub async fn create_proof(
        &self,
        method: &str,
        params: &JsonValue,
    ) -> Result<Vec<u8>, ColibriError> {
        let params_json = serde_json::to_string(params)?;
        let mut prover = Prover::new(
            method,
            &params_json,
            self.config.chain_id,
            self.config.prover_flags(),
        )?;

        loop {
            let raw = prover.execute_json_status()?;
            match Status::parse(&raw)? {
                Status::Pending { requests } => {
                    self.handle_requests(&requests, false).await;
                }
                Status::Success { .. } => return prover.get_proof(),
                Status::Revert { data } => return Err(RevertError { data }.into()),
                Status::Error { message } => {
                    return Err(crate::types::ProofError::Generation(message).into());
                }
            }
        }
    }

    /// Verify a previously generated proof and return the verified
    /// result. Reverts produce [`ColibriError::Revert`].
    pub async fn verify_proof(
        &self,
        proof: &[u8],
        method: &str,
        params: &JsonValue,
    ) -> Result<JsonValue, ColibriError> {
        let params_json = serde_json::to_string(params)?;
        let mut verifier = Verifier::new(
            proof,
            method,
            &params_json,
            self.config.chain_id,
            self.config.trusted_checkpoint.as_deref(),
            self.config.verify_flags(),
        )?;
        verifier.set_min_latest_block_ts(self.config.min_latest_block_ts());

        loop {
            let raw = verifier.execute_json_status()?;
            match Status::parse(&raw)? {
                Status::Pending { requests } => {
                    self.handle_requests(&requests, true).await;
                }
                Status::Success { result } => return Ok(result.unwrap_or(JsonValue::Null)),
                Status::Revert { data } => return Err(RevertError { data }.into()),
                Status::Error { message } => {
                    return Err(crate::types::VerificationError::Failed(message).into());
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Request handling.
    // ------------------------------------------------------------------

    async fn handle_requests(&self, requests: &[DataRequest], use_prover_fallback: bool) {
        let futs = requests
            .iter()
            .map(|req| self.handle_single_request(req, use_prover_fallback));
        // Errors here have already been forwarded to the C core via
        // `c4_req_set_error`; the outer `join_all` swallows them.
        join_all(futs).await;
    }

    async fn handle_single_request(&self, req: &DataRequest, use_prover_fallback: bool) {
        // Respect the delay hint used by e.g. oblivious-node retries.
        if req.delay > 0 {
            tokio::time::sleep(Duration::from_millis(req.delay as u64)).await;
        }

        // Injected mock / user-supplied handler wins -- used by the
        // testing module, by `http_fetch`, and as the wasm transport
        // (no built-in reqwest there).
        //
        // Safety: `req.req_ptr` originates from a `pending` status
        // returned by the very context we are still executing (see
        // `Colibri::rpc` / `create_proof` / `verify_proof`). It stays
        // valid until the next `execute_json_status` call, which is
        // strictly after this fanout completes.
        if let Some(handler) = self.request_handler.as_ref() {
            match handler.handle_with_fallback(req, use_prover_fallback).await {
                Ok((bytes, node_index)) => unsafe {
                    req_set_response(req.req_ptr, &bytes, node_index)
                },
                Err(err) => unsafe {
                    let _ = req_set_error(req.req_ptr, &err.to_string(), 0);
                },
            }
            return;
        }

        // No handler -> fall back to the built-in reqwest transport on
        // native targets. wasm targets have no default transport and
        // need `http_fetch` or a `RequestHandler`.
        #[cfg(not(target_family = "wasm"))]
        {
            let servers = self.config.pick_servers(req, use_prover_fallback);
            if servers.is_empty() {
                unsafe {
                    let _ = req_set_error(
                        req.req_ptr,
                        &format!("no servers configured for {:?}", req.request_type),
                        0,
                    );
                }
                return;
            }

            match self.execute_http(req, &servers).await {
                Ok((bytes, node_index)) => unsafe {
                    req_set_response(req.req_ptr, &bytes, node_index)
                },
                Err(err) => unsafe {
                    let _ = req_set_error(req.req_ptr, &err.to_string(), 0);
                },
            }
        }
        #[cfg(target_family = "wasm")]
        {
            let _ = use_prover_fallback;
            unsafe {
                let _ = req_set_error(
                    req.req_ptr,
                    "colibri-stateless: no built-in HTTP transport on wasm targets; \
                     configure ColibriBuilder::http_fetch() or a RequestHandler",
                    0,
                );
            }
        }
    }

    #[cfg(not(target_family = "wasm"))]
    async fn execute_http(
        &self,
        req: &DataRequest,
        servers: &[String],
    ) -> Result<(Vec<u8>, u16), ColibriError> {
        let mut last_error: Option<ColibriError> = None;

        // The C core caps the node list at C4_MAX_NODES (currently 16)
        // and the exclude_mask is a `u32`. We cap our iteration at 32
        // so `1u32 << i` never overflows -- Rust `<<` on out-of-range
        // shifts panics in debug and yields `0` in release, silently
        // dropping the exclusion.
        for (i, server) in servers.iter().enumerate().take(32) {
            if req.exclude_mask & (1u32 << i) != 0 {
                continue;
            }
            let attempt = FetchRequest::from_data_request(server, req)?;
            let url = attempt.url.clone();
            let mut builder = match attempt.method {
                HttpMethod::Get => self.http.get(&url),
                HttpMethod::Post => self.http.post(&url),
                HttpMethod::Put => self.http.put(&url),
                HttpMethod::Delete => self.http.delete(&url),
            };
            for (name, value) in attempt.headers() {
                builder = builder.header(name, value);
            }
            if let Some(body) = attempt.body {
                builder = builder.body(body);
            }

            match builder.send().await {
                Ok(resp) => {
                    let status = resp.status();
                    match resp.bytes().await {
                        Ok(bytes) if status.is_success() => {
                            return Ok((bytes.to_vec(), i as u16));
                        }
                        Ok(bytes) => {
                            last_error = Some(
                                crate::types::HttpError::full(
                                    String::from_utf8_lossy(&bytes).into_owned(),
                                    status.as_u16(),
                                    &url,
                                )
                                .into(),
                            );
                        }
                        Err(e) => last_error = Some(e.into()),
                    }
                }
                Err(e) => last_error = Some(e.into()),
            }
        }

        Err(last_error.unwrap_or_else(|| crate::types::HttpError::new("all servers failed").into()))
    }
}
