# Colibri Stateless — Swift

**Verify Ethereum RPC data cryptographically — without running a full node.**

![ETH2.0 Spec Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

Colibri Stateless is a highly efficient prover/verifier for Ethereum (with upcoming support for Layer-2s such as OP-Stack). This native Swift Package wraps the C core and verifies every RPC response against the beacon chain — no full node, no continuous sync.

[**Website**](https://www.corpuscore.tech/colibri) · [**Docs**](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/swift) · [**Whitepaper**](https://corpus-core.gitbook.io/whitepaper-colibri-stateless) · [**Privacy (PAP)**](https://corpus-core.gitbook.io/pap-colibri-stateless)

## Why Colibri?

- **Stateless** — verification needs nothing but the proof and the sync committee it is checked against. The committee is cached locally so it does not have to travel with every request, but it works just as well with an empty cache or none at all. No persistent state, no full node.
- **Cryptographically verified RPC** — every RPC response is checked against BLS signatures.
- **Offline verification** — proofs are fully self-contained and verify without any network connection, thanks to zk-proofs for the sync committee and signed checkpoints.
- **On-demand, not always-on** — work happens only when you make a request; no background sync burning bandwidth, CPU, or battery.
- **Verifies historical data (older than ~27h / 8192 blocks)** — via `historical_summaries` proofs, where other light clients simply fail.
- **`eth_getLogs` completeness proofs** — optional `logsCompleteness = true` proves no matching log was omitted in the requested range.
- **Fully verified local transaction simulation** — simulate a transaction against verified state before signing.
- **Privacy-aware** — Pragmatic Adaptive Privacy (PAP) mode. *Experimental.*

## Quick Start

### iOS

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/corpus-core/colibri-stateless-swift.git", from: "1.0.0")
]

// App Code
import Colibri

let colibri = Colibri()
colibri.chainId = 1

let balance = try await colibri.rpc(method: "eth_getBalance", params: [
    "0x742d35Cc6434C532532532532532532535C0ddd", "latest"
])
```

### macOS Development

```bash
./build_macos.sh -dev
swift build && swift test
```

## Platform notes

- **iOS + macOS** — native Swift Package for Apple platforms.
- **Flexible storage** — customizable storage implementations (see below).
- **Well tested** — unit and integration tests with mock data.

## Examples

### Chains

```swift
colibri.chainId = 1      // Ethereum
colibri.chainId = 137    // Polygon  
colibri.chainId = 42161  // Arbitrum
```

### Privacy (PAP)

Enable Pragmatic Adaptive Privacy to reduce intent leakage (e.g. use cached storage when available):

*This feature is still experimental!*

```swift
colibri.privacyMode = .basic  // default: .none
```

### Privacy-preserving `eth_call`

Use hybrid + PAP + oblivious nodes (`obliviousNodes` default `[]`). Hybrid: block proof only from prover. PAP: no access-list leak to prover. Oblivious: TEE for `eth_getProof` (auto-enables PAP). `https://rpc.safe-node.com/` needs an API key for testing. Background on TEE/ORAM: [Oblivious Labs](https://www.obliviouslabs.com/).

```swift
colibri.privacyMode = .basic
colibri.proverMode = .hybrid
colibri.obliviousNodes = ["https://rpc.safe-node.com/"]
```

### Custom Storage

```swift
class MyStorage: ColibriStorage {
    func get(key: String) -> Data? { /* ... */ }
    func set(key: String, value: Data) { /* ... */ }
    func delete(key: String) { /* ... */ }
}

StorageBridge.registerStorage(MyStorage())
```

## Build

```bash
# iOS XCFramework
./build_ios.sh

# macOS Development
./build_macos.sh -dev
```

## Testing

```bash
swift test                                     # All tests
swift test --filter ColibriTests             # Unit tests  
swift test --filter GeneratedIntegrationTests # Integration tests
cd test_ios_app && swift test                # iOS example
```

## Documentation

- **[Complete Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/swift)** - Full API reference and guide
- **[iOS Test App](test_ios_app/)** - Reference implementation & CI example
- **[Local Documentation](doc.md)** - Source documentation
- **[Core Repository](https://github.com/corpus-core/colibri-stateless)** - Source code
