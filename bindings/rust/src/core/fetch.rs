//! Default HTTP routing for hosts that supply only the byte transport.
//!
//! The native [`Colibri`](super::Colibri) client uses `reqwest` to
//! fetch pending [`DataRequest`]s. Wasm targets cannot compile that
//! stack, so the host must provide a fetch function. [`FetchRequestHandler`]
//! (and [`ColibriBuilder::http_fetch`](super::ColibriBuilder::http_fetch))
//! reuse the same endpoint selection, URL joining and header policy as
//! the native path -- the closure only performs one HTTP round-trip.

use std::future::Future;
use std::sync::Arc;

use async_trait::async_trait;
use futures::future::BoxFuture;
use serde_json::Value as JsonValue;

use super::client::{ColibriConfig, RequestHandler};
use super::helpers::get_current_version_number;
use crate::types::{ColibriError, DataRequest, HttpError, HttpMethod, RequestType};

/// One concrete HTTP attempt after endpoint routing.
///
/// [`FetchRequestHandler`] builds this from a pending [`DataRequest`]
/// plus a chosen server URL. Hosts that only want to run a GET/POST
/// against `url` can ignore the remaining fields; [`headers`](Self::headers)
/// reconstructs what the native `reqwest` transport would send.
#[derive(Debug, Clone)]
pub struct FetchRequest {
    /// HTTP verb.
    pub method: HttpMethod,
    /// Absolute URL (`{server}/{req.url}`).
    pub url: String,
    /// `Accept` header value (`application/json` or
    /// `application/octet-stream`).
    pub accept: &'static str,
    /// JSON body, when the core supplied a payload.
    pub body: Option<Vec<u8>>,
    /// Optional TTL hint in seconds (`Cache-Control: max-age=<ttl>`).
    pub ttl: u32,
    /// Endpoint kind the core asked for (informs extra headers such as
    /// `Colibri-Version` on prover requests).
    pub request_type: RequestType,
}

impl FetchRequest {
    /// Build a [`FetchRequest`] targeting `server` for `req`.
    pub fn from_data_request(server: &str, req: &DataRequest) -> Result<Self, ColibriError> {
        let body = req.payload.as_ref().map(serde_json::to_vec).transpose()?;
        Ok(Self {
            method: req.method,
            url: join_url(server, &req.url),
            accept: req.encoding.accept_header(),
            body,
            ttl: req.ttl,
            request_type: req.request_type,
        })
    }

    /// Headers the native `reqwest` transport would send for this
    /// attempt, in send order.
    pub fn headers(&self) -> Vec<(&'static str, String)> {
        let mut headers = vec![("Accept", self.accept.to_string())];
        if self.ttl > 0 {
            headers.push(("Cache-Control", format!("max-age={}", self.ttl)));
        }
        if self.request_type == RequestType::Prover {
            headers.push(("Colibri-Version", get_current_version_number().to_string()));
        }
        if self.body.is_some() {
            headers.push(("Content-Type", "application/json".to_string()));
        }
        headers
    }
}

/// Join a server base URL with the path the C core requested.
pub fn join_url(server: &str, path: &str) -> String {
    if path.is_empty() {
        server.trim_end_matches('/').to_string()
    } else {
        format!(
            "{}/{}",
            server.trim_end_matches('/'),
            path.trim_start_matches('/')
        )
    }
}

/// Boxed fetch callback stored on [`FetchRequestHandler`] and on
/// [`ColibriBuilder`](super::ColibriBuilder).
pub(crate) type DynFetcher =
    Arc<dyn Fn(FetchRequest) -> BoxFuture<'static, Result<Vec<u8>, ColibriError>> + Send + Sync>;

/// [`RequestHandler`] that performs Colibri's endpoint routing and
/// delegates the actual HTTP round-trip to a user-supplied function.
///
/// Native hosts can keep using the built-in `reqwest` transport. Wasm
/// hosts (no sockets under WASI) install this via
/// [`ColibriBuilder::http_fetch`](super::ColibriBuilder::http_fetch)
/// so they only implement `GET`/`POST` against an absolute URL:
///
/// ```ignore
/// use colibri_stateless::{Colibri, FetchRequestHandler, MAINNET};
///
/// let client = Colibri::builder(MAINNET)
///     .provers(["https://mainnet.colibri-proof.tech"])
///     .http_fetch(|req| async move {
///         host_http(req.method.as_str(), &req.url, &req.headers(), req.body).await
///     })
///     .build();
/// ```
///
/// A full [`RequestHandler`] still wins over this helper when both are
/// configured.
pub struct FetchRequestHandler {
    config: ColibriConfig,
    fetch: DynFetcher,
}

impl FetchRequestHandler {
    /// Wrap `fetch` so it can be stored behind the [`RequestHandler`]
    /// trait object.
    pub fn new<F, Fut>(config: ColibriConfig, fetch: F) -> Self
    where
        F: Fn(FetchRequest) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = Result<Vec<u8>, ColibriError>> + Send + 'static,
    {
        Self::from_fetcher(config, Arc::new(move |req| Box::pin(fetch(req))))
    }

    pub(crate) fn from_fetcher(config: ColibriConfig, fetch: DynFetcher) -> Self {
        Self { config, fetch }
    }

    async fn dispatch(
        &self,
        req: &DataRequest,
        use_prover_fallback: bool,
    ) -> Result<(Vec<u8>, u16), ColibriError> {
        dispatch_fetch(&self.config, &self.fetch, req, use_prover_fallback).await
    }
}

#[async_trait]
impl RequestHandler for FetchRequestHandler {
    async fn handle(&self, request: &DataRequest) -> Result<Vec<u8>, ColibriError> {
        // Direct `handle()` calls (tests, manual dispatch) use the
        // `rpc` / `verify_proof` fallback hint. `create_proof` goes
        // through `handle_with_fallback(..., false)` instead.
        self.dispatch(request, true).await.map(|(bytes, _)| bytes)
    }

    async fn handle_with_fallback(
        &self,
        request: &DataRequest,
        use_prover_fallback: bool,
    ) -> Result<(Vec<u8>, u16), ColibriError> {
        self.dispatch(request, use_prover_fallback).await
    }
}

/// Walk the server list for `req` and invoke `fetch` until one attempt
/// succeeds. Mirrors the native `reqwest` loop in `Colibri`.
pub(crate) async fn dispatch_fetch(
    config: &ColibriConfig,
    fetch: &DynFetcher,
    req: &DataRequest,
    use_prover_fallback: bool,
) -> Result<(Vec<u8>, u16), ColibriError> {
    let servers = config.pick_servers(req, use_prover_fallback);
    if servers.is_empty() {
        return Err(
            HttpError::new(format!("no servers configured for {:?}", req.request_type)).into(),
        );
    }

    let mut last_error: Option<ColibriError> = None;
    // Cap at 32 so `1u32 << i` never overflows (the C core's
    // exclude_mask is a `u32`).
    for (i, server) in servers.iter().enumerate().take(32) {
        if req.exclude_mask & (1u32 << i) != 0 {
            continue;
        }
        let attempt = match FetchRequest::from_data_request(server, req) {
            Ok(a) => a,
            Err(e) => return Err(e),
        };
        match fetch(attempt).await {
            Ok(bytes) => return Ok((bytes, i as u16)),
            Err(e) => last_error = Some(e),
        }
    }

    Err(last_error.unwrap_or_else(|| HttpError::new("all servers failed").into()))
}

impl ColibriConfig {
    /// Candidate endpoints for `req`, matching the native `reqwest`
    /// transport. `use_prover_fallback` is `true` for [`Colibri::rpc`]
    /// / [`Colibri::verify_proof`] (Beacon API may fall back to the
    /// prover list) and `false` for [`Colibri::create_proof`].
    pub fn pick_servers(&self, req: &DataRequest, use_prover_fallback: bool) -> Vec<String> {
        match req.request_type {
            RequestType::Checkpointz => {
                let mut v = self.checkpointz.clone();
                v.extend(self.beacon_apis.iter().cloned());
                v
            }
            RequestType::Prover => self.provers.clone(),
            RequestType::BeaconApi => {
                if use_prover_fallback && !self.provers.is_empty() {
                    self.provers.clone()
                } else {
                    self.beacon_apis.clone()
                }
            }
            RequestType::EthRpc => {
                if is_get_proof(req) && !self.oblivious_nodes.is_empty() {
                    self.oblivious_nodes.clone()
                } else {
                    self.eth_rpcs.clone()
                }
            }
            // `intern`, `cache`, `rest_api` -- fall back to the generic
            // eth_rpc list; the C core does not normally emit these to
            // the host.
            _ => self.eth_rpcs.clone(),
        }
    }
}

fn is_get_proof(req: &DataRequest) -> bool {
    matches!(
        req.payload.as_ref().and_then(|p| p.get("method")),
        Some(JsonValue::String(m)) if m == "eth_getProof"
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{Encoding, HttpMethod, MAINNET};
    use async_trait::async_trait;
    use std::sync::Mutex;

    fn data_req(request_type: RequestType, url: &str, exclude_mask: u32) -> DataRequest {
        DataRequest {
            req_ptr: 0,
            chain_id: MAINNET,
            encoding: Encoding::Json,
            exclude_mask,
            delay: 0,
            ttl: 0,
            method: HttpMethod::Post,
            url: url.to_string(),
            payload: None,
            request_type,
        }
    }

    fn cfg() -> ColibriConfig {
        let mut c = ColibriConfig::new(MAINNET);
        c.provers = vec!["https://prover.example".into()];
        c.eth_rpcs = vec![
            "https://rpc-a.example".into(),
            "https://rpc-b.example".into(),
        ];
        c.beacon_apis = vec!["https://beacon.example".into()];
        c.checkpointz = vec!["https://checkpointz.example".into()];
        c.oblivious_nodes = vec!["https://oram.example".into()];
        c
    }

    #[test]
    fn join_url_empty_path_keeps_base() {
        assert_eq!(join_url("https://p.example/", ""), "https://p.example");
        assert_eq!(join_url("https://p.example", ""), "https://p.example");
        assert_eq!(
            join_url("https://p.example/", "/eth/v1/foo"),
            "https://p.example/eth/v1/foo"
        );
        assert_eq!(
            join_url("https://p.example", "eth/v1/foo"),
            "https://p.example/eth/v1/foo"
        );
    }

    #[test]
    fn encoding_accept_header() {
        assert_eq!(Encoding::Json.accept_header(), "application/json");
        assert_eq!(Encoding::Ssz.accept_header(), "application/octet-stream");
    }

    #[test]
    fn pick_servers_eth_rpc_and_oblivious_get_proof() {
        let c = cfg();
        let eth = data_req(RequestType::EthRpc, "", 0);
        assert_eq!(c.pick_servers(&eth, true), c.eth_rpcs);

        let mut proof = data_req(RequestType::EthRpc, "", 0);
        proof.payload = Some(serde_json::json!({"method": "eth_getProof"}));
        assert_eq!(c.pick_servers(&proof, true), c.oblivious_nodes);
    }

    #[test]
    fn pick_servers_beacon_fallback_and_checkpointz() {
        let c = cfg();
        let beacon = data_req(RequestType::BeaconApi, "/eth/v1/x", 0);
        assert_eq!(c.pick_servers(&beacon, true), c.provers);
        assert_eq!(c.pick_servers(&beacon, false), c.beacon_apis);

        let cp = data_req(RequestType::Checkpointz, "", 0);
        let mut expected = c.checkpointz.clone();
        expected.extend(c.beacon_apis.iter().cloned());
        assert_eq!(c.pick_servers(&cp, true), expected);
    }

    #[test]
    fn pick_servers_prover_and_wildcard_types() {
        let c = cfg();
        let prover = data_req(RequestType::Prover, "", 0);
        assert_eq!(c.pick_servers(&prover, true), c.provers);
        assert_eq!(c.pick_servers(&prover, false), c.provers);

        for ty in [
            RequestType::RestApi,
            RequestType::Intern,
            RequestType::Cache,
        ] {
            let req = data_req(ty, "", 0);
            assert_eq!(
                c.pick_servers(&req, true),
                c.eth_rpcs,
                "wildcard type {ty:?} should use eth_rpcs"
            );
        }
    }

    #[test]
    fn pick_servers_skips_empty_fallback_lists() {
        let mut c = cfg();
        c.provers.clear();
        let beacon = data_req(RequestType::BeaconApi, "/eth/v1/x", 0);
        assert_eq!(
            c.pick_servers(&beacon, true),
            c.beacon_apis,
            "empty prover list must not steal BeaconApi routing"
        );

        c = cfg();
        c.oblivious_nodes.clear();
        let mut proof = data_req(RequestType::EthRpc, "", 0);
        proof.payload = Some(serde_json::json!({"method": "eth_getProof"}));
        assert_eq!(
            c.pick_servers(&proof, true),
            c.eth_rpcs,
            "eth_getProof without oblivious nodes uses eth_rpcs"
        );

        proof.payload = Some(serde_json::json!({"method": "eth_getBalance"}));
        assert_eq!(c.pick_servers(&proof, true), cfg().eth_rpcs);
    }

    #[test]
    fn fetch_request_headers_and_body() {
        let mut req = data_req(RequestType::EthRpc, "x", 0);
        req.encoding = Encoding::Ssz;
        req.ttl = 60;
        req.payload = Some(serde_json::json!({"method": "eth_blockNumber"}));
        req.method = HttpMethod::Get;

        let fr = FetchRequest::from_data_request("https://rpc.example/", &req)
            .expect("serialise payload");
        assert_eq!(fr.method, HttpMethod::Get);
        assert_eq!(fr.url, "https://rpc.example/x");
        assert_eq!(fr.accept, "application/octet-stream");
        assert_eq!(
            fr.body.as_deref(),
            Some(br#"{"method":"eth_blockNumber"}"#.as_slice())
        );
        assert_eq!(
            fr.headers(),
            vec![
                ("Accept", "application/octet-stream".into()),
                ("Cache-Control", "max-age=60".into()),
                ("Content-Type", "application/json".into()),
            ]
        );

        let bare = FetchRequest::from_data_request(
            "https://rpc.example",
            &data_req(RequestType::EthRpc, "", 0),
        )
        .unwrap();
        assert_eq!(bare.body, None);
        assert_eq!(bare.headers(), vec![("Accept", "application/json".into())]);

        // Prover attempts advertise the wire-format version (same header as
        // the native reqwest path). The numeric value comes from the C
        // library; we only assert the header is present and parseable.
        let prover = FetchRequest::from_data_request(
            "https://prover.example",
            &data_req(RequestType::Prover, "proof", 0),
        )
        .unwrap();
        let headers = prover.headers();
        assert_eq!(headers[0], ("Accept", "application/json".into()));
        assert_eq!(headers[1].0, "Colibri-Version");
        assert!(
            headers[1].1.parse::<u32>().is_ok(),
            "Colibri-Version must be a decimal u32, got {}",
            headers[1].1
        );
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_skips_excluded_and_retries() {
        let attempted: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let log = attempted.clone();
        let handler = FetchRequestHandler::new(cfg(), move |req: FetchRequest| {
            let log = log.clone();
            async move {
                log.lock().unwrap().push(req.url.clone());
                if req.url.contains("rpc-b") {
                    Ok(b"ok".to_vec())
                } else {
                    Err(HttpError::new("fail").into())
                }
            }
        });

        // exclude_mask bit 0 skips rpc-a; first live attempt is rpc-b.
        let req = data_req(RequestType::EthRpc, "path", 0b01);
        let body = handler.handle(&req).await.expect("rpc-b succeeds");
        assert_eq!(body, b"ok");
        assert_eq!(
            *attempted.lock().unwrap(),
            vec!["https://rpc-b.example/path".to_string()]
        );
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_retries_next_server_on_error() {
        let attempted: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let log = attempted.clone();
        let handler = FetchRequestHandler::new(cfg(), move |req: FetchRequest| {
            let log = log.clone();
            async move {
                log.lock().unwrap().push(req.url.clone());
                if req.url.contains("rpc-a") {
                    Err(HttpError::new("down").into())
                } else {
                    Ok(b"ok".to_vec())
                }
            }
        });

        let req = data_req(RequestType::EthRpc, "", 0);
        let body = handler.handle(&req).await.expect("retry succeeds");
        assert_eq!(body, b"ok");
        assert_eq!(
            *attempted.lock().unwrap(),
            vec![
                "https://rpc-a.example".to_string(),
                "https://rpc-b.example".to_string()
            ]
        );
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_no_servers_is_error() {
        let mut c = cfg();
        c.provers.clear();
        let handler = FetchRequestHandler::new(c, |_req| async { Ok(vec![]) });
        let req = data_req(RequestType::Prover, "", 0);
        let err = handler.handle(&req).await.expect_err("empty list");
        match err {
            ColibriError::Http(e) => assert!(e.message.contains("no servers")),
            other => panic!("unexpected {other}"),
        }
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_all_excluded_is_all_servers_failed() {
        let handler = FetchRequestHandler::new(cfg(), |_req| async {
            panic!("fetch must not run when every server is excluded")
        });
        // Two eth_rpcs; bits 0 and 1 skip both.
        let req = data_req(RequestType::EthRpc, "", 0b11);
        let err = handler.handle(&req).await.expect_err("all excluded");
        match err {
            ColibriError::Http(e) => assert!(e.message.contains("all servers failed")),
            other => panic!("unexpected {other}"),
        }
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_all_fail_returns_last_error() {
        let handler = FetchRequestHandler::new(cfg(), |req: FetchRequest| async move {
            Err(HttpError::new(format!("down:{url}", url = req.url)).into())
        });
        let req = data_req(RequestType::EthRpc, "", 0);
        let err = handler.handle(&req).await.expect_err("all fail");
        match err {
            ColibriError::Http(e) => assert_eq!(e.message, "down:https://rpc-b.example"),
            other => panic!("unexpected {other}"),
        }
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_honors_fallback_flag() {
        let attempted: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let log = attempted.clone();
        let handler = FetchRequestHandler::new(cfg(), move |req: FetchRequest| {
            let log = log.clone();
            async move {
                log.lock().unwrap().push(req.url.clone());
                Ok(b"ok".to_vec())
            }
        });

        let req = data_req(RequestType::BeaconApi, "/eth/v1/x", 0);

        handler
            .handle_with_fallback(&req, true)
            .await
            .expect("fallback to prover");
        handler
            .handle_with_fallback(&req, false)
            .await
            .expect("direct beacon");
        // `handle()` must match `rpc` / `verify_proof` (fallback = true).
        handler.handle(&req).await.expect("handle uses fallback");

        assert_eq!(
            *attempted.lock().unwrap(),
            vec![
                "https://prover.example/eth/v1/x".to_string(),
                "https://beacon.example/eth/v1/x".to_string(),
                "https://prover.example/eth/v1/x".to_string(),
            ]
        );
    }

    struct DefaultHandler;

    #[async_trait]
    impl RequestHandler for DefaultHandler {
        async fn handle(&self, request: &DataRequest) -> Result<Vec<u8>, ColibriError> {
            Ok(format!("echo:{}", request.url).into_bytes())
        }
    }

    #[tokio::test(flavor = "current_thread")]
    async fn default_handler_ignores_fallback_flag() {
        let h = DefaultHandler;
        let req = data_req(RequestType::BeaconApi, "/eth/v1/x", 0);
        let with_fallback = h.handle_with_fallback(&req, true).await.unwrap();
        let without_fallback = h.handle_with_fallback(&req, false).await.unwrap();
        assert_eq!(with_fallback, without_fallback);
        assert_eq!(with_fallback, (b"echo:/eth/v1/x".to_vec(), 0));
    }

    #[tokio::test(flavor = "current_thread")]
    async fn fetch_handler_reports_successful_node_index() {
        let handler = FetchRequestHandler::new(cfg(), |req: FetchRequest| async move {
            if req.url.contains("rpc-a") {
                Err(HttpError::new("down").into())
            } else {
                Ok(b"ok".to_vec())
            }
        });
        let req = data_req(RequestType::EthRpc, "", 0);
        let (body, index) = handler
            .handle_with_fallback(&req, true)
            .await
            .expect("rpc-b");
        assert_eq!(body, b"ok");
        assert_eq!(index, 1);
    }
}
