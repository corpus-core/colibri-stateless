# Colibri Swift Bindings

Native Swift Package for secure, verified blockchain interactions.

## 🚀 Quick Start

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

## ✨ Features

- **🔐 Cryptographic Verification** - Merkle proofs for all RPC responses
- **📱 iOS + macOS** - Native Swift Package for Apple platforms  
- **🗄️ Flexible Storage** - Customizable storage implementations
- **🧪 28 Tests** - Unit + Integration tests with mock data

## 📖 Examples

### Chains

```swift
colibri.chainId = 1      // Ethereum
colibri.chainId = 137    // Polygon  
colibri.chainId = 42161  // Arbitrum
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

## 🏗️ Build

```bash
# iOS XCFramework
./build_ios.sh

# macOS Development
./build_macos.sh -dev
```

## 🧪 Testing

```bash
swift test                                     # All tests
swift test --filter ColibriTests             # Unit tests  
swift test --filter GeneratedIntegrationTests # Integration tests
cd test_ios_app && swift test                # iOS example
```

## 📚 Documentation

- **[📖 Complete Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/swift)** - Full API reference and guide
- **[�� iOS Test App](test_ios_app/)** - Reference implementation & CI example
- **[📄 Local Documentation](doc.md)** - Source documentation
- **[�� Core Repository](https://github.com/corpus-core/colibri-stateless)** - Source code
