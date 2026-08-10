//! Test helpers -- fixture discovery + file-backed mocks.
//!
//! Mirrors [`bindings/python/src/colibri/testing.py`][py]: the same
//! `test/data/*/test.json` fixtures used by the Python, Dart and Swift
//! bindings can be replayed here without touching the network.
//!
//! The helpers are behind the always-enabled `testing` module rather
//! than a feature flag so that both `cargo test` inside the crate and
//! downstream integration tests can use them.
//!
//! [py]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/python/src/colibri/testing.py

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use async_trait::async_trait;
use serde::Deserialize;
use serde_json::Value as JsonValue;

use crate::storage::Storage;
use crate::types::{ColibriError, DataRequest};

/// Maximum length of a mock filename base (matches
/// `C4_MAX_MOCKNAME_LEN` in `src/util/state.c`).
const MAX_MOCKNAME_LEN: usize = 100;

/// A single fixture loaded from `test/data/<name>/test.json`.
#[derive(Debug, Clone, Deserialize)]
pub struct TestCase {
    /// Fixture directory name.
    #[serde(skip)]
    pub name: String,
    /// Absolute path to the fixture directory (contains state /
    /// response files).
    #[serde(skip)]
    pub directory: PathBuf,

    /// RPC method under test.
    pub method: String,
    /// RPC parameters as raw JSON (matches the on-disk fixture).
    pub params: JsonValue,
    /// Chain ID (`1`, `11155111`, ...).
    pub chain_id: u64,
    /// `true` when the fixture contains an `expected_result` key --
    /// including `expected_result: null`, which downstream tests must
    /// validate rather than skip.
    #[serde(skip)]
    pub has_expected_result: bool,
    /// Expected verified result. Present only when
    /// `has_expected_result` is `true`; the wrapped value may be
    /// `JsonValue::Null`.
    #[serde(default)]
    pub expected_result: JsonValue,
    /// Fixture opts into Pragmatic Adaptive Privacy mode.
    #[serde(default)]
    pub pap: bool,
    /// Fixture requires a remote prover (mock URL is registered by
    /// the runner).
    #[serde(default)]
    pub remote_prover: bool,
    /// Include contract code in the proof.
    #[serde(default)]
    pub include_code: bool,
    /// Fixture uses `eth_createAccessList` (default `true`).
    #[serde(default = "default_true")]
    pub use_accesslist: bool,
    /// Fixture requires a full chain-store snapshot -- skipped by the
    /// discovery helper.
    #[serde(default)]
    pub requires_chain_store: bool,
}

fn default_true() -> bool {
    true
}

/// Discover fixtures under `root` (typically `<repo>/test/data`).
///
/// Fixtures with `requires_chain_store: true` are silently skipped so
/// they don't fail on a stateless verifier.
pub fn discover_tests(root: impl AsRef<Path>) -> Vec<TestCase> {
    let root = root.as_ref();
    let mut out = Vec::new();
    let Ok(entries) = fs::read_dir(root) else {
        return out;
    };
    for entry in entries.flatten() {
        let dir = entry.path();
        if !dir.is_dir() {
            continue;
        }
        let json_path = dir.join("test.json");
        let Ok(raw) = fs::read_to_string(&json_path) else {
            continue;
        };
        // Two-pass parse: `serde_json::Value` first so we can
        // distinguish "no expected_result key" from
        // "expected_result: null" (which needs to be validated, not
        // skipped -- see the `pap_tx_pending` fixture).
        let Ok(raw_json) = serde_json::from_str::<JsonValue>(&raw) else {
            eprintln!(
                "colibri-stateless: invalid test.json: {}",
                json_path.display()
            );
            continue;
        };
        let has_expected = raw_json.get("expected_result").is_some();
        let Ok(mut tc) = serde_json::from_value::<TestCase>(raw_json) else {
            eprintln!(
                "colibri-stateless: invalid test.json shape: {}",
                json_path.display()
            );
            continue;
        };
        if tc.requires_chain_store {
            continue;
        }
        tc.has_expected_result = has_expected;
        tc.name = dir
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        tc.directory = dir;
        out.push(tc);
    }
    out.sort_by(|a, b| a.name.cmp(&b.name));
    out
}

/// Locate the repository's `test/data` directory. Walks up from the
/// crate's manifest directory until it finds `test/data/`. Returns
/// `None` when the crate is used outside the repository (e.g. after
/// `cargo publish`).
pub fn find_test_data_root() -> Option<PathBuf> {
    let mut dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    for _ in 0..6 {
        let candidate = dir.join("test").join("data");
        if candidate.is_dir() {
            return Some(candidate);
        }
        if !dir.pop() {
            break;
        }
    }
    None
}

// ---------------------------------------------------------------------
// File-based mock storage (mirrors Python's FileBasedMockStorage).
// ---------------------------------------------------------------------

/// [`Storage`] backed by files in a fixture directory. Reads are
/// cached; writes and deletes stay in memory (they only exist so the C
/// core can maintain its own state during the run).
pub struct FileBackedMockStorage {
    dir: PathBuf,
    cache: Mutex<HashMap<String, Option<Vec<u8>>>>,
    access_count: Mutex<HashMap<String, u32>>,
    max_access: u32,
}

impl FileBackedMockStorage {
    /// Create a mock storage reading files from `dir`.
    pub fn new(dir: impl Into<PathBuf>) -> Self {
        Self {
            dir: dir.into(),
            cache: Mutex::new(HashMap::new()),
            access_count: Mutex::new(HashMap::new()),
            // Same safety limit as the Python implementation.
            max_access: 5,
        }
    }

    fn resolve(&self, key: &str) -> Option<PathBuf> {
        let direct = self.dir.join(key);
        if direct.exists() {
            return Some(direct);
        }
        // Filenames can be truncated at 200-255 chars on macOS.
        if key.len() > 200 {
            let (base, ext) = key
                .rsplit_once('.')
                .map(|(b, e)| (b, Some(e)))
                .unwrap_or((key, None));
            for prefix_len in [250, 240, 230, 220, 200, 150, 100] {
                if base.len() > prefix_len {
                    let prefix = &base[..prefix_len];
                    if let Ok(entries) = fs::read_dir(&self.dir) {
                        for e in entries.flatten() {
                            let name = e.file_name().to_string_lossy().into_owned();
                            if !name.starts_with(prefix) {
                                continue;
                            }
                            if let Some(ext) = ext {
                                if name.ends_with(ext) {
                                    return Some(e.path());
                                }
                            } else {
                                return Some(e.path());
                            }
                        }
                    }
                }
            }
        }
        None
    }
}

impl FileBackedMockStorage {
    /// Refresh timestamps inside `tx_pending_*` entries so the TTL
    /// check inside the C core always passes. Each entry is 40 bytes:
    /// 32 bytes tx_hash + 8 bytes little-endian unix timestamp. Mirrors
    /// [`FileBasedMockStorage._refresh_pending_timestamps`][py].
    ///
    /// [py]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/python/src/colibri/testing.py
    fn refresh_pending_timestamps(mut data: Vec<u8>) -> Vec<u8> {
        use std::time::{SystemTime, UNIX_EPOCH};
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or_default();
        let now_le = now.to_le_bytes();
        let mut i = 0usize;
        while i + 40 <= data.len() {
            data[i + 32..i + 40].copy_from_slice(&now_le);
            i += 40;
        }
        data
    }
}

impl Storage for FileBackedMockStorage {
    fn get(&self, key: &str) -> Option<Vec<u8>> {
        {
            let mut counts = self.access_count.lock().unwrap();
            let entry = counts.entry(key.to_string()).or_insert(0);
            *entry += 1;
            if *entry > self.max_access {
                return self.cache.lock().unwrap().get(key).cloned().flatten();
            }
        }
        {
            let cache = self.cache.lock().unwrap();
            if let Some(cached) = cache.get(key) {
                return cached.clone();
            }
        }
        let path = self.resolve(key);
        let data = path.and_then(|p| fs::read(p).ok()).map(|d| {
            if key.starts_with("tx_pending_") {
                Self::refresh_pending_timestamps(d)
            } else {
                d
            }
        });
        self.cache
            .lock()
            .unwrap()
            .insert(key.to_string(), data.clone());
        data
    }

    fn set(&self, key: &str, value: &[u8]) {
        self.cache
            .lock()
            .unwrap()
            .insert(key.to_string(), Some(value.to_vec()));
    }

    fn delete(&self, key: &str) {
        self.cache.lock().unwrap().insert(key.to_string(), None);
    }
}

// ---------------------------------------------------------------------
// File-based mock request handler.
// ---------------------------------------------------------------------

/// [`RequestHandler`] backed by fixture files stored next to the
/// `test.json` file. Filenames follow the same convention as
/// `c4_req_mockname` in `src/util/state.c` (used across bindings).
pub struct FileBackedMockRequestHandler {
    dir: PathBuf,
    request_count: Mutex<u32>,
    max_requests: u32,
}

impl FileBackedMockRequestHandler {
    /// Create a handler serving fixtures from `dir`.
    pub fn new(dir: impl Into<PathBuf>) -> Self {
        Self {
            dir: dir.into(),
            request_count: Mutex::new(0),
            max_requests: 50,
        }
    }

    fn sanitise(s: &str) -> String {
        s.chars()
            .map(|c| match c {
                '/' | '.' | ',' | ' ' | ':' | '=' | '?' | '"' | '&' | '[' | ']' | '{' | '}' => '_',
                _ => c,
            })
            .collect()
    }

    fn base_name(request: &DataRequest) -> String {
        if !request.url.is_empty() {
            // Cache-friendly proof URLs of the form
            // `proof/<method>/<block>/<version>/<zk|std>/<c4>` are
            // compressed to `proof/<method>/<block>` so fixtures stay
            // stable across version bumps.
            let mut base = request.url.clone();
            if let Some(rest) = base.strip_prefix("proof/") {
                if let Some(first) = rest.find('/') {
                    if let Some(second) = rest[first + 1..].find('/') {
                        let cut = first + 1 + second;
                        base = format!("proof/{}", &rest[..cut]);
                    }
                }
            }
            return Self::sanitise(&base);
        }

        if let Some(payload) = request.payload.as_ref() {
            let method = payload.get("method").and_then(|m| m.as_str()).unwrap_or("");
            let params = payload
                .get("params")
                .and_then(|p| p.as_array())
                .cloned()
                .unwrap_or_default();
            let mut base = method.to_string();
            for p in params {
                let s = match p {
                    JsonValue::String(s) => s,
                    other => serde_json::to_string(&other).unwrap_or_default(),
                };
                base.push('_');
                base.push_str(&s);
            }
            return Self::sanitise(&base);
        }

        "unknown".into()
    }

    fn resolve_response(&self, request: &DataRequest) -> Option<PathBuf> {
        let mut base = Self::base_name(request);
        if base.len() > MAX_MOCKNAME_LEN {
            base.truncate(MAX_MOCKNAME_LEN);
        }
        let filename = format!("{base}.{}", request.encoding);

        let direct = self.dir.join(&filename);
        if direct.exists() {
            return Some(direct);
        }

        // Long-name fallback.
        let store = FileBackedMockStorage::new(self.dir.clone());
        if let Some(path) = store.resolve(&filename) {
            return Some(path);
        }

        // Best-effort fallbacks for beacon endpoints that carry a slot
        // in their URL.
        let hints = [
            ("light_client_updates", "*light_client_updates*"),
            ("beacon/headers", "*headers*"),
            ("beacon/blocks", "*blocks*"),
        ];
        for (needle, glob) in hints {
            if filename.contains(needle) || request.url.contains(needle) {
                if let Ok(entries) = fs::read_dir(&self.dir) {
                    for e in entries.flatten() {
                        let name = e.file_name().to_string_lossy().into_owned();
                        if glob_match(glob, &name) {
                            return Some(e.path());
                        }
                    }
                }
            }
        }
        None
    }
}

fn glob_match(pattern: &str, name: &str) -> bool {
    // Very small subset: `*foo*` -> contains "foo".
    let trimmed = pattern.trim_matches('*');
    trimmed.is_empty() || name.contains(trimmed)
}

#[async_trait]
impl super::core::RequestHandler for FileBackedMockRequestHandler {
    async fn handle(&self, request: &DataRequest) -> Result<Vec<u8>, ColibriError> {
        {
            let mut count = self.request_count.lock().unwrap();
            *count += 1;
            if *count > self.max_requests {
                return Err(ColibriError::Ffi(format!(
                    "too many mock requests ({}); likely infinite loop",
                    *count
                )));
            }
        }
        match self.resolve_response(request) {
            Some(path) => fs::read(&path).map_err(|e| {
                ColibriError::Ffi(format!("reading mock fixture {}: {e}", path.display()))
            }),
            None => {
                let base = Self::base_name(request);
                let mut trimmed = base.clone();
                if trimmed.len() > MAX_MOCKNAME_LEN {
                    trimmed.truncate(MAX_MOCKNAME_LEN);
                }
                let filename = format!("{trimmed}.{}", request.encoding);
                Err(ColibriError::Ffi(format!(
                    "no mock fixture for request (url={:?}, method={:?}, filename={filename})",
                    request.url, request.method
                )))
            }
        }
    }
}
