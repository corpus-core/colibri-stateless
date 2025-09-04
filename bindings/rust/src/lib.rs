use libc::{c_char, c_int, c_uint, c_uchar, c_void};
use once_cell::sync::OnceCell;
use serde::{Deserialize, Serialize};
use serde_json::Value as JsonValue;
use serde::de::{self, Deserializer};
use std::ffi::{CStr, CString};
use std::future::Future;
use std::pin::Pin;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, RwLock};
use thiserror::Error;

// ===== FFI =====

#[repr(C)]
pub struct Bytes {
    pub len: u32,
    pub data: *mut c_uchar,
}

extern "C" {
    // proofer
    fn c4_create_proofer_ctx(method: *mut c_char, params: *mut c_char, chain_id: u64, flags: u32) -> *mut c_void;
    fn c4_proofer_execute_json_status(ctx: *mut c_void) -> *mut c_char;
    fn c4_proofer_get_proof(ctx: *mut c_void) -> Bytes;
    fn c4_free_proofer_ctx(ctx: *mut c_void);

    // request wiring
    fn c4_req_set_response(req_ptr: *mut c_void, data: Bytes, node_index: u16);
    fn c4_req_set_error(req_ptr: *mut c_void, error: *mut c_char, node_index: u16);

    // verify
    fn c4_verify_create_ctx(proof: Bytes, method: *mut c_char, args: *mut c_char, chain_id: u64, trusted_block_hashes: *mut c_char) -> *mut c_void;
    fn c4_verify_execute_json_status(ctx: *mut c_void) -> *mut c_char;
    fn c4_verify_free_ctx(ctx: *mut c_void);

    // method support
    fn c4_get_method_support(chain_id: u64, method: *mut c_char) -> c_int;
}

// Storage plugin FFI
#[repr(C)]
pub struct Buffer {
    pub data: Bytes,
    pub allocated: i32,
}

#[repr(C)]
pub struct StoragePlugin {
    pub get: Option<extern "C" fn(*mut c_char, *mut Buffer) -> bool>,
    pub set: Option<extern "C" fn(*mut c_char, Bytes) -> ()>,
    pub del: Option<extern "C" fn(*mut c_char) -> ()>,
    pub max_sync_states: u32,
}

extern "C" {
    fn c4_get_storage_config(plugin: *mut StoragePlugin);
    fn c4_set_storage_config(plugin: *mut StoragePlugin);
}

// ===== Public API Types =====

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RpcCall {
    pub method: String,
    pub params: JsonValue, // JSON-Array i.d.R.
}

#[derive(Debug, Clone, Default)]
pub struct Config {
    pub rpc_urls: Vec<String>,
    pub beacon_urls: Vec<String>,
    pub proofer_urls: Vec<String>,
    pub chain_id: u64,
    pub trusted_block_hashes: Vec<String>,
}

#[derive(Debug, Error)]
pub enum ColibriError {
    #[error("FFI null pointer")] 
    NullPtr,
    #[error("C returned invalid JSON: {0}")] 
    InvalidJson(String),
    #[error("Proofer error: {0}")] 
    Proofer(String),
    #[error("Verifier error: {0}")] 
    Verifier(String),
    #[error("HTTP error: {0}")] 
    Http(String),
}

// DataRequest gemäß C-JSON
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DataRequest {
    #[serde(deserialize_with = "de_u64")]
    pub req_ptr: u64,
    pub chain_id: Option<u64>,
    pub encoding: Option<String>,
    #[serde(default, deserialize_with = "de_opt_u32")]
    pub exclude_mask: Option<u32>,
    pub method: Option<String>,
    pub url: Option<String>,
    pub payload: Option<JsonValue>,
    pub r#type: Option<String>,
}

fn de_u64<'de, D>(deserializer: D) -> Result<u64, D::Error>
where
    D: Deserializer<'de>,
{
    let v: serde_json::Value = Deserialize::deserialize(deserializer)?;
    match v {
        serde_json::Value::Number(n) => n.as_u64().ok_or_else(|| de::Error::custom("invalid u64 number")),
        serde_json::Value::String(s) => s.parse::<u64>().map_err(|e| de::Error::custom(format!("{}", e))),
        _ => Err(de::Error::custom("expected number or string for u64")),
    }
}

fn de_opt_u32<'de, D>(deserializer: D) -> Result<Option<u32>, D::Error>
where
    D: Deserializer<'de>,
{
    let v: Option<serde_json::Value> = Option::deserialize(deserializer)?;
    match v {
        None => Ok(None),
        Some(serde_json::Value::Null) => Ok(None),
        Some(serde_json::Value::Number(n)) => n
            .as_u64()
            .and_then(|x| u32::try_from(x).ok())
            .ok_or_else(|| de::Error::custom("invalid u32 number"))
            .map(Some),
        Some(serde_json::Value::String(s)) => s
            .parse::<u32>()
            .map(Some)
            .map_err(|e| de::Error::custom(format!("{}", e))),
        Some(other) => Err(de::Error::custom(format!("unexpected type for u32: {}", other))),
    }
}

pub trait RequestHandler: Send + Sync + 'static {
    fn handle<'a>(&'a self, req: DataRequest) -> Pin<Box<dyn Future<Output = Result<Vec<u8>, String>> + Send + 'a>>;
}

static STORAGE_SET: AtomicBool = AtomicBool::new(false);
static STORAGE_IMPL: RwLock<Option<Arc<dyn Storage + Send + Sync>>> = RwLock::new(None);

pub trait Storage {
    fn get(&self, key: &str) -> Option<Vec<u8>>;
    fn set(&self, key: &str, value: &[u8]);
    fn delete(&self, key: &str);
}

fn parse_c_json(s: &str) -> Result<JsonValue, serde_json::Error> {
    match serde_json::from_str::<JsonValue>(s) {
        Ok(v) => Ok(v),
        Err(e) => {
            // Workaround: trailing comma vor schließender Klammer entfernen
            let fixed = s.replace(",}\n", "}\n").replace(",}", "}");
            if fixed != s {
                serde_json::from_str(&fixed)
            } else {
                Err(e)
            }
        }
    }
}

// C storage callbacks
extern "C" fn storage_get(key: *mut c_char, out: *mut Buffer) -> bool {
    let storage = STORAGE_IMPL.read().ok().and_then(|o| o.clone());
    let Some(storage) = storage else { return false; };
    if key.is_null() || out.is_null() { return false; }
    let c_key = unsafe { CStr::from_ptr(key) };
    let key_str = match c_key.to_str() { Ok(s) => s, Err(_) => return false };
    eprintln!("[rust][storage_get] key={}", key_str);
    if let Some(data) = storage.get(key_str) {
        // allocate via libc malloc; C Seite wird free'n
        let len = data.len() as u32;
        let ptr = unsafe { libc::malloc(len as usize) as *mut c_uchar };
        if ptr.is_null() { return false; }
        unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), ptr, len as usize); }
        let buf = Buffer { data: Bytes { len, data: ptr }, allocated: len as i32 };
        unsafe { *out = buf; }
        eprintln!("[rust][storage_get] -> {} bytes", len);
        true
    } else {
        eprintln!("[rust][storage_get] -> not found");
        unsafe { (*out).data = Bytes { len: 0, data: null_mut() }; (*out).allocated = 0; }
        false
    }
}

extern "C" fn storage_set(key: *mut c_char, val: Bytes) {
    let storage = STORAGE_IMPL.read().ok().and_then(|o| o.clone());
    let Some(storage) = storage else { return; };
    if key.is_null() { return; }
    let c_key = unsafe { CStr::from_ptr(key) };
    if let Ok(key_str) = c_key.to_str() {
        let slice = unsafe { std::slice::from_raw_parts(val.data, val.len as usize) };
        eprintln!("[rust][storage_set] key={} ({} bytes)", key_str, slice.len());
        storage.set(key_str, slice);
    }
}

extern "C" fn storage_del(key: *mut c_char) {
    let storage = STORAGE_IMPL.read().ok().and_then(|o| o.clone());
    let Some(storage) = storage else { return; };
    if key.is_null() { return; }
    let c_key = unsafe { CStr::from_ptr(key) };
    if let Ok(key_str) = c_key.to_str() {
        eprintln!("[rust][storage_del] key={}", key_str);
        storage.delete(key_str);
    }
}

fn ensure_storage_bridge() {
    if STORAGE_SET.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_ok() {
        unsafe {
            let mut plugin = StoragePlugin {
                get: Some(storage_get),
                set: Some(storage_set),
                del: Some(storage_del),
                max_sync_states: 0,
            };
            c4_set_storage_config(&mut plugin as *mut _);
        }
    }
}

pub fn register_storage<S: Storage + Send + Sync + 'static>(storage: S) {
    if let Ok(mut w) = STORAGE_IMPL.write() {
        *w = Some(Arc::new(storage));
    }
    ensure_storage_bridge();
}

#[derive(Clone)]
pub struct Colibri {
    config: Config,
    handler: Option<Arc<dyn RequestHandler>>,
}

impl Colibri {
    pub fn new(config: Config) -> Self { Self { config, handler: None } }
    pub fn with_handler(mut self, handler: Arc<dyn RequestHandler>) -> Self { self.handler = Some(handler); self }

    pub async fn get_method_support(&self, method: &str) -> i32 {
        let c_method = CString::new(method).unwrap();
        let ptr = c_method.into_raw();
        let ty = unsafe { c4_get_method_support(self.config.chain_id, ptr) };
        // reclaim and drop
        unsafe { let _ = CString::from_raw(ptr); }
        ty as i32
    }

    pub async fn create_proof(&self, call: &RpcCall) -> Result<Vec<u8>, ColibriError> {
        let method = CString::new(call.method.clone()).unwrap();
        let params = CString::new(serde_json::to_string(&call.params).unwrap()).unwrap();
        let method_ptr = method.into_raw();
        let params_ptr = params.into_raw();
        let ctx = unsafe { c4_create_proofer_ctx(method_ptr, params_ptr, self.config.chain_id, 0) };
        // free duplicated inputs allocated by us
        unsafe { let _ = CString::from_raw(method_ptr); let _ = CString::from_raw(params_ptr); }
        if ctx.is_null() { return Err(ColibriError::NullPtr); }
        let result = self.drive_proofer_ctx(ctx).await;
        unsafe { c4_free_proofer_ctx(ctx) };
        result
    }

    pub async fn verify_proof(&self, call: &RpcCall, proof: &[u8]) -> Result<JsonValue, ColibriError> {
        let method = CString::new(call.method.clone()).unwrap();
        let params = CString::new(serde_json::to_string(&call.params).unwrap()).unwrap();
        let trusted = CString::new(serde_json::to_string(&self.config.trusted_block_hashes).unwrap()).unwrap();
        let bytes = Bytes { len: proof.len() as u32, data: proof.as_ptr() as *mut c_uchar };
        let method_ptr = method.into_raw();
        let params_ptr = params.into_raw();
        let trusted_ptr = trusted.into_raw();
        let ctx = unsafe { c4_verify_create_ctx(bytes, method_ptr, params_ptr, self.config.chain_id, trusted_ptr) };
        // free our inputs
        unsafe { let _ = CString::from_raw(method_ptr); let _ = CString::from_raw(params_ptr); let _ = CString::from_raw(trusted_ptr); }
        if ctx.is_null() { return Err(ColibriError::NullPtr); }
        let result = self.drive_verify_ctx(ctx).await;
        unsafe { c4_verify_free_ctx(ctx) };
        result
    }

    pub async fn rpc(&self, call: &RpcCall) -> Result<JsonValue, ColibriError> {
        // Falls proofer_urls gesetzt sind, zuerst Remote-Proof versuchen
        let proof = if !self.config.proofer_urls.is_empty() {
            match self.fetch_proof_from_proofer(call).await {
                Ok(p) => p,
                Err(e) => {
                    eprintln!("[rust][rpc] proofer fetch failed: {} -> fallback local", e);
                    self.create_proof(call).await?
                }
            }
        } else {
            self.create_proof(call).await?
        };
        self.verify_proof(call, &proof).await
    }

    #[cfg(not(target_arch = "wasm32"))]
    async fn fetch_proof_from_proofer(&self, call: &RpcCall) -> Result<Vec<u8>, ColibriError> {
        let payload = serde_json::json!({
            "id": 1,
            "jsonrpc": "2.0",
            "method": call.method,
            "params": call.params,
        });
        let client = reqwest::Client::new();
        let mut last_err: Option<String> = None;
        for (idx, url) in self.config.proofer_urls.iter().enumerate() {
            eprintln!("[rust][proofer] try {} -> {}", idx, url);
            match client
                .post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/octet-stream")
                .json(&payload)
                .send()
                .await
            {
                Ok(resp) => {
                    if resp.status().is_success() {
                        let bytes = resp.bytes().await.map_err(|e| ColibriError::Http(e.to_string()))?;
                        eprintln!("[rust][proofer] ok {} bytes", bytes.len());
                        return Ok(bytes.to_vec());
                    } else {
                        let status = resp.status();
                        let txt = resp.text().await.unwrap_or_default();
                        last_err = Some(format!("HTTP {} {}", status, txt));
                    }
                }
                Err(e) => last_err = Some(e.to_string()),
            }
        }
        Err(ColibriError::Http(last_err.unwrap_or_else(|| "All proofers failed".into())))
    }
    #[cfg(target_arch = "wasm32")]
    async fn fetch_proof_from_proofer(&self, _call: &RpcCall) -> Result<Vec<u8>, ColibriError> {
        Err(ColibriError::Http("not supported on wasm32".into()))
    }

    async fn drive_proofer_ctx(&self, ctx: *mut c_void) -> Result<Vec<u8>, ColibriError> {
        let mut iterations = 0u32;
        loop {
            iterations += 1;
            if iterations > 500 { return Err(ColibriError::Proofer("exceeded max iterations".into())); }
            let json_ptr = unsafe { c4_proofer_execute_json_status(ctx) };
            if json_ptr.is_null() { return Err(ColibriError::NullPtr); }
            let s = unsafe { CStr::from_ptr(json_ptr).to_string_lossy().into_owned() };
            // C Seite allokiert; wir müssen free() aufrufen
            unsafe { libc::free(json_ptr as *mut c_void) };
            let v: JsonValue = parse_c_json(&s).map_err(|e| ColibriError::InvalidJson(format!("{} | {}", e, s)))?;
            match v["status"].as_str() {
                Some("success") => {
                    let b = unsafe { c4_proofer_get_proof(ctx) };
                    let out = unsafe { std::slice::from_raw_parts(b.data, b.len as usize).to_vec() };
                    return Ok(out);
                }
                Some("error") => {
                    let msg = v["error"].as_str().unwrap_or("unknown error").to_string();
                    return Err(ColibriError::Proofer(msg));
                }
                Some("pending") => {
                    if let Some(reqs) = v["requests"].as_array() {
                        for r in reqs {
                            let req: DataRequest = serde_json::from_value(r.clone()).unwrap();
                            self.handle_request(req).await;
                        }
                    }
                }
                _ => return Err(ColibriError::InvalidJson(s)),
            }
        }
    }

    async fn drive_verify_ctx(&self, ctx: *mut c_void) -> Result<JsonValue, ColibriError> {
        let mut iterations = 0u32;
        loop {
            iterations += 1;
            if iterations > 500 { return Err(ColibriError::Verifier("exceeded max iterations".into())); }
            let json_ptr = unsafe { c4_verify_execute_json_status(ctx) };
            if json_ptr.is_null() { return Err(ColibriError::NullPtr); }
            let s = unsafe { CStr::from_ptr(json_ptr).to_string_lossy().into_owned() };
            unsafe { libc::free(json_ptr as *mut c_void) };
            let v: JsonValue = parse_c_json(&s).map_err(|e| ColibriError::InvalidJson(format!("{} | {}", e, s)))?;
            match v["status"].as_str() {
                Some("success") => {
                    return Ok(v.get("result").cloned().unwrap_or(JsonValue::Null));
                }
                Some("error") => {
                    let msg = v["error"].as_str().unwrap_or("unknown error").to_string();
                    return Err(ColibriError::Verifier(msg));
                }
                Some("pending") => {
                    if let Some(reqs) = v["requests"].as_array() {
                        for r in reqs {
                            let req: DataRequest = serde_json::from_value(r.clone()).unwrap();
                            self.handle_request(req).await;
                        }
                    }
                }
                _ => return Err(ColibriError::InvalidJson(s)),
            }
        }
    }

    async fn handle_request(&self, req: DataRequest) {
        // 1) Handler (Mock) -> direkt setzen
        if let Some(h) = &self.handler {
            match h.handle(req.clone()).await {
                Ok(bytes) => {
                    eprintln!("[rust][req mock ok] type={:?} url={:?} method={:?} encoding={:?} len={}", req.r#type, req.url, req.method, req.encoding, bytes.len());
                    unsafe { c4_req_set_response(req.req_ptr as *mut c_void, Bytes { len: bytes.len() as u32, data: bytes.as_ptr() as *mut c_uchar }, 0) };
                }
                Err(err) => {
                    eprintln!("[rust][req mock err] type={:?} url={:?} method={:?} encoding={:?} err={}", req.r#type, req.url, req.method, req.encoding, err);
                    let c = CString::new(err).unwrap();
                    let ptr = c.into_raw();
                    unsafe { c4_req_set_error(req.req_ptr as *mut c_void, ptr, 0); let _ = CString::from_raw(ptr); }
                }
            }
            return;
        }
        // 2) Real HTTP (reqwest)
        #[cfg(target_arch = "wasm32")]
        {
            let msg = CString::new("network unsupported on wasm32 in native crate").unwrap();
            let ptr = msg.into_raw();
            unsafe { c4_req_set_error(req.req_ptr as *mut c_void, ptr, 0); let _ = CString::from_raw(ptr); }
            return;
        }
        #[cfg(not(target_arch = "wasm32"))]
        let servers = match req.r#type.as_deref() {
            Some("beacon_api") => if !self.config.proofer_urls.is_empty() { &self.config.proofer_urls } else { &self.config.beacon_urls },
            _ => &self.config.rpc_urls,
        };
        #[cfg(not(target_arch = "wasm32"))]
        let mut last_err = None;
        #[cfg(not(target_arch = "wasm32"))]
        for (idx, base) in servers.iter().enumerate() {
            if req.exclude_mask.unwrap_or(0) & (1 << idx) != 0 { continue; }
            let url = if let Some(u) = &req.url { format!("{}/{}", base.trim_end_matches('/'), u.trim_start_matches('/')) } else { base.clone() };
            let method = req.method.as_deref().unwrap_or("POST");
            let accept = req.encoding.as_deref().unwrap_or("json");
            eprintln!("[rust][req http] idx={} base={} -> url={} method={} accept={} type={:?}", idx, base, url, method, accept, req.r#type);
            let client = reqwest::Client::new();
            let mut rb = client.request(method.parse().unwrap_or(reqwest::Method::POST), &url);
            if accept == "json" { rb = rb.header("Accept", "application/json"); } else { rb = rb.header("Accept", "application/octet-stream"); }
            if let Some(p) = &req.payload { rb = rb.json(p); }
            match rb.send().await {
                Ok(resp) => {
                    if resp.status().is_success() {
                        match resp.bytes().await {
                            Ok(b) => unsafe {
                                eprintln!("[rust][req http ok] idx={} len={}", idx, b.len());
                                c4_req_set_response(req.req_ptr as *mut c_void, Bytes { len: b.len() as u32, data: b.as_ptr() as *mut c_uchar }, idx as u16);
                            },
                            Err(e) => { last_err = Some(e.to_string()); continue; }
                        }
                        return;
                    } else {
                        eprintln!("[rust][req http err] status={}", resp.status());
                        last_err = Some(format!("HTTP {}", resp.status()))
                    }
                }
                Err(e) => { last_err = Some(e.to_string()); }
            }
        }
        #[cfg(not(target_arch = "wasm32"))]
        {
            let msg = CString::new(last_err.unwrap_or_else(|| "All nodes failed".into())).unwrap();
            let ptr = msg.into_raw();
            unsafe { c4_req_set_error(req.req_ptr as *mut c_void, ptr, 0); let _ = CString::from_raw(ptr); }
        }
    }
}

// ===== Test Helpers =====
#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Result;
    use serial_test::serial;
    use std::fs;
    use std::path::PathBuf;

    struct MockStorage { base: PathBuf }
    impl Storage for MockStorage {
        fn get(&self, key: &str) -> Option<Vec<u8>> {
            // try exact
            let p = self.base.join(key);
            if let Ok(b) = fs::read(&p) { return Some(b); }
            // try with common extensions
            for ext in ["ssz", "json", "bin"] {
                let candidate = self.base.join(format!("{}.{}", key, ext));
                if let Ok(b) = fs::read(&candidate) { return Some(b); }
            }
            None
        }
        fn set(&self, _key: &str, _value: &[u8]) {}
        fn delete(&self, _key: &str) {}
    }

    struct FileRequestHandler { base: PathBuf }
    impl RequestHandler for FileRequestHandler {
        fn handle<'a>(&'a self, req: DataRequest) -> Pin<Box<dyn Future<Output = Result<Vec<u8>, String>> + Send + 'a>> {
            Box::pin(async move {
                // Namebildung analog Swift/Kotlin: payload bevorzugen
                let mut name = String::new();
                if let Some(p) = req.payload.clone() {
                    if let Some(m) = p.get("method").and_then(|v| v.as_str()) { name.push_str(m); }
                    if let Some(params) = p.get("params").and_then(|v| v.as_array()) {
                        for par in params {
                            name.push('_');
                            name.push_str(&par.to_string());
                        }
                    }
                } else if let Some(u) = req.url.as_ref() { name = u.clone(); }
                // sanitize
                let forbidden = ["/", "\\", ".", ",", " ", ":", "\"", "&", "=", "[", "]", "{", "}", "?"]; 
                let mut sanitized = String::with_capacity(name.len());
                for ch in name.chars() {
                    let s = ch.to_string();
                    if forbidden.contains(&s.as_str()) { sanitized.push('_'); } else { sanitized.push(ch); }
                }
                if sanitized.len() > 100 { sanitized.truncate(100); }
                let ext = req.encoding.clone().unwrap_or_else(|| "json".into());
                let file = self.base.join(format!("{}.{}", sanitized, ext));
                if let Ok(b) = fs::read(&file) { return Ok(b); }

                // Fallbacks ähnlich Swift/Kotlin/JS
                let mut entries: Vec<String> = Vec::new();
                if let Ok(rd) = fs::read_dir(&self.base) {
                    for e in rd.flatten() {
                        if let Some(name) = e.file_name().to_str() { entries.push(name.to_string()); }
                    }
                }

                // URL-basierte Fallbacks
                if let Some(u) = req.url.as_ref() {
                    let parts: Vec<&str> = u.split(|c| c == '/' || c == '?').filter(|s| !s.is_empty()).collect();
                    if parts.contains(&"headers") && parts.contains(&"head") {
                        if let Some(found) = entries.iter().find(|n| n.contains("headers")) {
                            return fs::read(self.base.join(found)).map_err(|e| format!("{}", e));
                        }
                    }
                    if parts.contains(&"blocks") && parts.contains(&"head") {
                        if let Some(found) = entries.iter().find(|n| n.contains("blocks") && !n.contains("head")) {
                            return fs::read(self.base.join(found)).map_err(|e| format!("{}", e));
                        }
                    }
                    if parts.contains(&"light_client") && parts.contains(&"updates") {
                        if let Some(found) = entries.iter().find(|n| n.contains("light_client_updates")) {
                            return fs::read(self.base.join(found)).map_err(|e| format!("{}", e));
                        }
                    }
                }

                // Method-basierte Fallbacks
                if let Some(p) = req.payload.clone() {
                    if let Some(m) = p.get("method").and_then(|v| v.as_str()) {
                        let mut candidates: Vec<&String> = entries.iter().filter(|n| n.starts_with(m)).collect();
                        if candidates.len() == 1 {
                            return fs::read(self.base.join(candidates[0])).map_err(|e| format!("{}", e));
                        } else if candidates.len() > 1 {
                            // Nimm die erste passende Datei
                            return fs::read(self.base.join(candidates[0])).map_err(|e| format!("{}", e));
                        }
                        // Smart-Fallback für bestimmte Methoden
                        let mut fallbacks: &[&str] = &[];
                        match m {
                            "eth_getBalance" | "eth_getStorageAt" | "eth_getCode" | "eth_getTransactionCount" => {
                                fallbacks = &["eth_getProof"]; }
                            _ => {}
                        }
                        for fb in fallbacks {
                            if let Some(found) = entries.iter().find(|n| n.starts_with(fb)) {
                                return fs::read(self.base.join(found)).map_err(|e| format!("{}", e));
                            }
                        }
                    }
                }

                Err(format!("Mock file not found for {}", sanitized))
            })
        }
    }

    include!(concat!(env!("OUT_DIR"), "/generated_tests.rs"));
}


