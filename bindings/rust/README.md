<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="Colibri Logo" width="300"/>

# colibri-stateless

[![Crates.io](https://img.shields.io/crates/v/colibri-stateless.svg)](https://crates.io/crates/colibri-stateless)
[![Documentation](https://docs.rs/colibri-stateless/badge.svg)](https://docs.rs/colibri-stateless)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](../../LICENSE)
[![ETH2.0 Spec 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)](https://github.com/ethereum/consensus-specs)

Rust bindings for [Colibri Stateless](https://github.com/corpus-core/colibri-stateless) --
an ultra-light prover / verifier for Ethereum and OP-Stack chains.

- Cryptographically verifies every RPC response against Merkle / SSZ
  proofs -- no reliance on centralised RPC providers.
- `async/await` API on `tokio`, using `reqwest` (rustls) internally.
- Same C core, same test fixtures as the Python / Dart / Kotlin /
  Swift bindings; nothing chain-specific lives in the Rust layer.
- Ships without vendored C sources on crates.io; `build.rs`
  downloads a matching prebuilt archive from the corresponding GitHub
  Release, or builds from source inside the monorepo.

## Quick start

```toml
[dependencies]
colibri-stateless = "0.1"
tokio = { version = "1", features = ["rt-multi-thread", "macros"] }
```

```rust
use colibri_stateless::Colibri;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let client = Colibri::builder(1)
        .provers(vec!["https://mainnet.colibri-proof.tech".into()])
        .build();

    let block = client.rpc("eth_blockNumber", &[]).await?;
    println!("current block = {block}");
    Ok(())
}
```

### Local proof generation (no remote prover)

```rust
use colibri_stateless::Colibri;
use serde_json::json;

let client = Colibri::builder(1)
    .provers(Vec::<String>::new())
    .eth_rpcs(vec!["https://eth.llamarpc.com".into()])
    .beacon_apis(vec!["https://lodestar-mainnet.chainsafe.io".into()])
    .build();

let balance = client
    .rpc(
        "eth_getBalance",
        &json!(["0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", "latest"]),
    )
    .await?;
```

## Features

| | |
|---|---|
| **Verified RPC** | `eth_call`, `eth_getBalance`, `eth_getLogs`, `eth_getProof`, `eth_getTransactionReceipt`, etc. |
| **Multiple modes** | `Local`, `Remote`, `Hybrid`, `Proxy` proof strategies |
| **Pluggable storage** | In-memory, file-system, or a custom `Storage` impl |
| **Pluggable transport** | Default `reqwest` handler, or your own `RequestHandler` |
| **PAP privacy** | Optional Pragmatic Adaptive Privacy mode (experimental) |
| **Shared fixtures** | Replays the monorepo `test/data/*` corpus via `colibri_stateless::testing` |

See the full API tour in the crate-level docs on
[docs.rs](https://docs.rs/colibri-stateless) or the developer guide on
[GitBook](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/rust).

## Native library distribution

The Rust crate is a thin `unsafe` shim on top of the `libc4.a` C
static archive. Where that archive comes from depends on your build:

- **`crates.io` install** -- `build.rs` downloads
  `colibri-native-<target>.tar.gz` from the GitHub Release matching
  the crate version. Supported targets: `x86_64-unknown-linux-gnu`,
  `aarch64-apple-darwin`, `x86_64-apple-darwin`,
  `x86_64-pc-windows-msvc`, `wasm32-wasip1` (WASI Preview 1, see
  [WebAssembly (wasm32-wasip1)](#webassembly-wasm32-wasip1) below).
- **Monorepo checkout** -- `build.rs` shells out to CMake and links
  the just-built archives. Requires CMake ≥ 3.20 and a C compiler. The
  CMake tree is placed in `build-rust/<target>/` at the repository
  root rather than under `target/`, because the vendored dependencies
  create paths that exceed Windows' 260-character `MAX_PATH` limit.
  Override the location with `COLIBRI_CMAKE_BUILD_DIR` if even that is
  too long for your checkout.
- **BYO archives** -- set `COLIBRI_LIB_DIR=/path/to/dir/with/archives`;
  every static archive in that directory is linked. Handy for embedded
  or cross-compilation targets not covered by the Release matrix.
- **docs.rs** -- the native build is skipped (`DOCS_RS` env var);
  only the API docs are produced.

## WebAssembly (wasm32-wasip1)

The crate compiles for `wasm32-wasip1` (WASI Preview 1) via the
[wasi-sdk][wasi-sdk] toolchain. Verifier and prover state machines
run inside the sandbox; TLS, DNS and sockets are not available under
WASI, so the built-in `reqwest` transport is **not** compiled for
wasm. Wasm hosts must therefore always supply their own
[`RequestHandler`][request-handler] -- without one, every pending
request is answered with an error.

- **crates.io**: the release asset `colibri-native-wasm32-wasip1.tar.gz`
  ships the static archives plus `libc++.a` / `libc++abi.a`; `wasi-libc`
  itself comes from `rustc`'s wasm sysroot. Nothing extra is needed
  on the consumer side.
- **Monorepo checkout**: set `WASI_SDK_PATH` to an unpacked
  [wasi-sdk][wasi-sdk] release (>= 22) so `build.rs` can pick up the
  `wasi-sdk-p1.cmake` toolchain file.
- **Runtime**: use `tokio` with `flavor = "current_thread"` (wasm has
  no `rt-multi-thread` feature). Prefer `MemoryStorage`; `FileStorage`
  only works under runtimes that mount host directories via preopens
  (`wasmtime --dir=<host>::<guest>`).

```rust,ignore
use async_trait::async_trait;
use colibri_stateless::{
    Colibri, ColibriError, DataRequest, Encoding, HttpError, HttpMethod,
    MemoryStorage, RequestHandler, RequestType, MAINNET,
};
use serde_json::json;
use std::sync::Arc;

/// HTTP transport supplied by the wasm host (e.g. a WASI import or the
/// `wasi:http` component). Replace with whatever your runtime offers.
async fn host_fetch(method: &str, url: &str, accept: &str, body: Option<&[u8]>)
    -> Result<Vec<u8>, String>
{
    todo!("call into the host")
}

struct WasiHandler {
    eth_rpcs: Vec<String>,
    beacon_apis: Vec<String>,
    provers: Vec<String>,
}

#[async_trait]
impl RequestHandler for WasiHandler {
    async fn handle(&self, req: &DataRequest) -> Result<Vec<u8>, ColibriError> {
        // The C core emits one of EthRpc / BeaconApi / Prover /
        // Checkpointz; other variants are handled internally.
        let servers = match req.request_type {
            RequestType::EthRpc => &self.eth_rpcs,
            RequestType::BeaconApi | RequestType::Checkpointz => &self.beacon_apis,
            _ => &self.provers,
        };
        let base = servers.iter().enumerate()
            .find(|(i, _)| req.exclude_mask & (1u32 << i) == 0)
            .map(|(_, s)| s)
            .ok_or_else(|| HttpError::new("no server left"))?;
        let url = format!("{}/{}", base.trim_end_matches('/'),
            req.url.trim_start_matches('/'));
        let accept = match req.encoding {
            Encoding::Json => "application/json",
            Encoding::Ssz => "application/octet-stream",
        };
        let body = req.payload.as_ref().map(serde_json::to_vec).transpose()?;
        let method = match req.method {
            HttpMethod::Get => "GET",
            HttpMethod::Post => "POST",
            HttpMethod::Put => "PUT",
            HttpMethod::Delete => "DELETE",
        };
        host_fetch(method, &url, accept, body.as_deref())
            .await
            .map_err(|e| HttpError::full(e, 0, url).into())
    }
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), ColibriError> {
    let client = Colibri::builder(MAINNET)
        .provers(vec!["https://mainnet.colibri-proof.tech".to_string()])
        .storage(MemoryStorage::new())
        .request_handler(Arc::new(WasiHandler {
            eth_rpcs: vec![],
            beacon_apis: vec![],
            provers: vec!["https://mainnet.colibri-proof.tech".to_string()],
        }))
        .build();

    let block = client.rpc("eth_blockNumber", &json!([])).await?;
    println!("verified block: {block}");
    Ok(())
}
```

Build and run:

```bash
rustup target add wasm32-wasip1
cargo build --release --target wasm32-wasip1
wasmtime target/wasm32-wasip1/release/app.wasm
```

[wasi-sdk]: https://github.com/WebAssembly/wasi-sdk
[request-handler]: https://docs.rs/colibri-stateless/latest/colibri_stateless/trait.RequestHandler.html

## Testing against shared fixtures

The same fixtures in `test/data/*` used by the C / Python / Dart /
Swift test suites are exposed as [`colibri_stateless::testing`](https://docs.rs/colibri-stateless/latest/colibri_stateless/testing/):

```rust
use std::sync::Arc;
use colibri_stateless::{Colibri, PrivacyMode};
use colibri_stateless::testing::{
    discover_tests, find_test_data_root,
    FileBackedMockRequestHandler, FileBackedMockStorage,
};

let root  = find_test_data_root().expect("test/data/ found");
let cases = discover_tests(&root);

for tc in cases {
    let handler = Arc::new(FileBackedMockRequestHandler::new(tc.directory.clone()));
    let storage = FileBackedMockStorage::new(tc.directory.clone());

    let client = Colibri::builder(tc.chain_id)
        .provers(if tc.remote_prover { vec!["http://mock-prover".into()] } else { vec![] })
        .privacy_mode(if tc.pap { PrivacyMode::Basic } else { PrivacyMode::None })
        .include_code(tc.include_code)
        .use_accesslist(tc.use_accesslist)
        .max_latest_age_seconds(0)
        .storage(storage)
        .request_handler(handler)
        .build();

    let result = client.rpc(&tc.method, &tc.params).await.unwrap();
    if let Some(expected) = tc.expected_result {
        assert_eq!(result, expected);
    }
}
```

## Documentation

- **Crate docs** -- [docs.rs/colibri-stateless](https://docs.rs/colibri-stateless)
- **User guide** -- [GitBook -- Rust bindings](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/rust)
- **Core repository** -- [github.com/corpus-core/colibri-stateless](https://github.com/corpus-core/colibri-stateless)

## Feedback

Bug reports and feature ideas go into the
[shared issue tracker](https://github.com/corpus-core/colibri-stateless/issues).
Please label Rust-specific issues with `bindings:rust`.

## License

Licensed under the MIT License (see [LICENSE](../../LICENSE)). Some
third-party libraries bundled with the C core carry their own
licenses -- see `libs/*/LICENSE` for details.
