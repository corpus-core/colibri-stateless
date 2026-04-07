: Bindings

:: Swift

Swift Package for integrating Colibri Stateless Client into iOS and macOS applications. These bindings provide a native Swift API for secure, verified blockchain interactions without trusting centralized infrastructure.

## Overview

The Colibri Swift Bindings enable you to verify Ethereum RPC calls with cryptographic proofs, directly in Swift applications. This provides Web3 functionality without dependency on centralized RPC providers.

### Core Features

- **🔐 Cryptographic Verification** - All RPC responses are validated with Merkle proofs
- **📱 iOS + macOS Support** - Native Swift Package for all Apple platforms
- **🗄️ Flexible Storage System** - Customizable storage implementations for different use cases
- **⚡ Performance** - Optimized C libraries with Swift interface
- **🧪 Comprehensive Testing** - Complete integration tests with mock data

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Swift Application Layer                      │
├─────────────────────────────────────────────────────────────────┤
│                     Colibri.swift API                           │
│  • Colibri class (main interface)                               │
│  • RequestHandler protocol                                      │
│  • ColibriStorage protocol                                      │
│  • Error handling & type conversion                             │
├─────────────────────────────────────────────────────────────────┤
│                  Swift-C Bridge Layer                           │
│  • swift_storage_bridge.c                                       │
│  • Function pointer callbacks                                   │
│  • Memory management                                            │
├─────────────────────────────────────────────────────────────────┤
│                   Core C Libraries                              │
│  • Prover (proof generation)                                   │
│  • Verifier (proof verification)                                │
│  • Storage plugin system                                        │
│  • Cryptographic libraries (blst, ed25519)                      │
└─────────────────────────────────────────────────────────────────┘
```

## Quick Start

### iOS Integration

For iOS applications, use the pre-built XCFramework:

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/corpus-core/colibri-stateless-swift.git", from: "1.0.0")
]
```

```swift
// In your iOS app
import Colibri

let colibri = Colibri()
colibri.chainId = 1  // Ethereum Mainnet
colibri.provers = ["https://c4.incubed.net"]

// RPC call with automatic proof verification
let result = try await colibri.rpc(method: "eth_getBalance", params: [
    "0x742d35Cc6434C532532532532532532535C0ddd",
    "latest"
])

if let balance = result as? String {
    print("Account balance: \(balance)")
}
```

### iOS Example App

The CI-Pipeline contains a minimalistic Example and TestApp for iOS, which is used to test the integration. You can look at the code as an example on how to use certain features.

The code can be found in the [bindings/swift/test_ios_app](https://github.com/corpus-core/colibri-stateless/tree/dev/bindings/swift/test_ios_app).

### macOS Development

For macOS development with local static libraries:

```bash
# 1. Build macOS libraries
./build_macos.sh -dev

# 2. Build Swift Package
swift build

# 3. Run tests
swift test
```

## Development Workflow

### Local macOS Build

```bash
# Fast development build (current architecture only, incremental)
./build_macos.sh -dev

# Production build (all architectures)
./build_macos.sh
```

**What happens during build:**

1. **Compile C Libraries** - All core libraries (Prover, Verifier, Crypto)
2. **Swift Storage Bridge** - C-Swift interop for storage system
3. **Generate Integration Tests** - Automatic generation of test functions from `test/data`
4. **Prepare Package** - Swift Package with all dependencies

### iOS XCFramework Build

```bash
# iOS XCFramework (Device + Simulator)
./build_ios.sh

# Creates: build_ios_arm/c4_swift.xcframework
```

**XCFramework Structure:**
```
c4_swift.xcframework/
├── ios-arm64/                    # iOS Device
│   └── c4_swift.framework/
├── ios-x86_64-simulator/         # iOS Simulator
│   └── c4_swift.framework/
└── Info.plist                    # Framework Metadata
```

### Test System

#### Unit Tests

```bash
swift test --filter ColibriTests
```

- **Initialization Tests** - Colibri setup and configuration
- **Method Support Tests** - RPC method availability
- **Storage Tests** - Custom storage implementations
- **Error Handling Tests** - Error handling and edge cases

#### Integration Tests

```bash
swift test --filter GeneratedIntegrationTests
```

- **23 automatically generated tests** from `test/data/*/test.json`
- **Mock HTTP Requests** - Offline tests with real blockchain data
- **Sequential Execution** - Storage is global, tests run sequentially
- **Result Verification** - Structural and string-based comparison

#### iOS Test App

```bash
cd test_ios_app
swift build && swift test
```

The iOS test app serves as:
- **CI Integration Test** - Verifies package integration in CI
- **Developer Example** - Reference implementation for iOS developers
- **API Demonstration** - Shows all important Colibri APIs

## API Reference

### Colibri Class

```swift
public class Colibri {
    /// Blockchain Chain ID (e.g. 1 for Ethereum Mainnet)
    public var chainId: UInt64
    
    /// List of Prover URLs (empty = local proof generation)
    public var provers: [String]
    
    /// Proof generation mode: .local, .remote (default), or .hybrid
    public var proverMode: ProverMode?
    
    /// PAP (Pragmatic Adaptive Privacy) mode: .none (default) or .basic
    public var privacyMode: PrivacyMode
    
    /// Initialization
    public init()
    
    /// RPC call with automatic proof verification
    public func rpc(method: String, params: [Any]) async throws -> Any
    
    /// Check if an RPC method is supported
    public func getMethodSupport(method: String) -> Bool
}
```

### Prover Mode

Controls how proofs are built and verified. Set via `proverMode`:

- **`.local`** -- Proofs are built entirely on the client. Requires access to a Beacon API and execution layer RPC. Fully trustless, but slower and needs more infrastructure.
- **`.remote`** -- Proofs are fetched from a remote Colibri prover server. Fastest option but relies on the prover server for proof generation. The verifier still cryptographically checks every proof.
- **`.hybrid`** -- The consensus-layer proof (BlockHeaderProof) comes from the Colibri server, while execution-layer data (account proofs, storage, etc.) is fetched directly from the RPC provider. Best balance of performance and scalability -- the Colibri server only serves lightweight, cacheable header proofs while the heavy RPC load goes to your existing provider.

```swift
let colibri = Colibri()
colibri.chainId = 1
colibri.provers = ["https://mainnet.colibri-proof.tech"]
colibri.proverMode = .hybrid
```

Default: `.remote` when prover URLs are configured, `.local` otherwise.

### Privacy (PAP)

**PAP (Pragmatic Adaptive Privacy)** reduces intent leakage towards RPC/prover by using cached data when available and verifying afterwards.

- `privacyMode` – `PrivacyMode.none` (default) or `PrivacyMode.basic`. With `.basic`, the verifier sets the PAP flag so that method-type and verification can use cached storage for optimistic execution (e.g. for `eth_call`); method type may depend on params.

```swift
let colibri = Colibri()
colibri.chainId = 1
colibri.privacyMode = .basic
```

### Storage System

```swift
public protocol ColibriStorage {
    /// Load data from storage
    func get(key: String) -> Data?
    
    /// Store data in storage
    func set(key: String, value: Data)
    
    /// Delete data from storage
    func delete(key: String)
}

/// Register custom storage
StorageBridge.registerStorage(myCustomStorage)
```

### Error Handling

```swift
public enum ColibriError: Error {
    case rpcError(String)
    case proofError(String)
    case networkError(String)
    case invalidParams(String)
}
```

## Storage Implementations

### Default File Storage

```swift
// Automatically activated, uses C4_STATES_DIR or current directory
let colibri = Colibri()  // DefaultFileStorage is automatically used
```

### UserDefaults Storage (iOS)

```swift
class UserDefaultsStorage: ColibriStorage {
    func get(key: String) -> Data? {
        return UserDefaults.standard.data(forKey: "colibri_\(key)")
    }
    
    func set(key: String, value: Data) {
        UserDefaults.standard.set(value, forKey: "colibri_\(key)")
    }
    
    func delete(key: String) {
        UserDefaults.standard.removeObject(forKey: "colibri_\(key)")
    }
}

StorageBridge.registerStorage(UserDefaultsStorage())
```

### Core Data Storage

```swift
class CoreDataStorage: ColibriStorage {
    private let context: NSManagedObjectContext
    
    init(context: NSManagedObjectContext) {
        self.context = context
    }
    
    func get(key: String) -> Data? {
        let request: NSFetchRequest<StorageEntity> = StorageEntity.fetchRequest()
        request.predicate = NSPredicate(format: "key == %@", key)
        request.fetchLimit = 1
        
        do {
            let entities = try context.fetch(request)
            return entities.first?.data
        } catch {
            print("Core Data fetch error: \(error)")
            return nil
        }
    }
    
    func set(key: String, value: Data) {
        // Check if already exists
        if let entity = fetchEntity(for: key) {
            entity.data = value
        } else {
            let entity = StorageEntity(context: context)
            entity.key = key
            entity.data = value
        }
        
        do {
            try context.save()
        } catch {
            print("Core Data save error: \(error)")
        }
    }
    
    func delete(key: String) {
        if let entity = fetchEntity(for: key) {
            context.delete(entity)
            try? context.save()
        }
    }
    
    private func fetchEntity(for key: String) -> StorageEntity? {
        let request: NSFetchRequest<StorageEntity> = StorageEntity.fetchRequest()
        request.predicate = NSPredicate(format: "key == %@", key)
        request.fetchLimit = 1
        return try? context.fetch(request).first
    }
}
```

## Chain Configurations

### Supported Chains

```swift
enum SupportedChain: UInt64 {
    case ethereum = 1
    case polygon = 137
    case arbitrum = 42161
    case base = 8453
    case optimism = 10
    
    var name: String {
        switch self {
        case .ethereum: return "Ethereum Mainnet"
        case .polygon: return "Polygon"
        case .arbitrum: return "Arbitrum One"
        case .base: return "Base"
        case .optimism: return "Optimism"
        }
    }
}

// Configure chain
colibri.chainId = SupportedChain.polygon.rawValue
```

### Multi-Chain Setup

```swift
class MultiChainColibri {
    private var clients: [UInt64: Colibri] = [:]
    
    func getClient(for chainId: UInt64) -> Colibri {
        if let client = clients[chainId] {
            return client
        }
        
        let client = Colibri()
        client.chainId = chainId
        client.proovers = ["https://c4.incubed.net"]
        clients[chainId] = client
        
        return client
    }
    
    func getBalance(account: String, chainId: UInt64) async throws -> String {
        let client = getClient(for: chainId)
        let result = try await client.rpc(method: "eth_getBalance", params: [account, "latest"])
        return result as? String ?? "0x0"
    }
}
```

## Performance Optimization

### Storage Performance

```swift
// Batch Storage Operations
class BatchingStorage: ColibriStorage {
    private var pendingWrites: [String: Data] = [:]
    private let batchSize = 100
    private let underlyingStorage: ColibriStorage
    
    init(underlying: ColibriStorage) {
        self.underlyingStorage = underlying
    }
    
    func set(key: String, value: Data) {
        pendingWrites[key] = value
        
        if pendingWrites.count >= batchSize {
            flushWrites()
        }
    }
    
    private func flushWrites() {
        for (key, value) in pendingWrites {
            underlyingStorage.set(key: key, value: value)
        }
        pendingWrites.removeAll()
    }
}
```

### Memory Management

```swift
// Storage with LRU Cache
class CachedStorage: ColibriStorage {
    private let cache = NSCache<NSString, NSData>()
    private let persistent: ColibriStorage
    
    init(persistent: ColibriStorage, maxCacheSize: Int = 100) {
        self.persistent = persistent
        cache.countLimit = maxCacheSize
    }
    
    func get(key: String) -> Data? {
        // Check cache first
        if let cached = cache.object(forKey: key as NSString) {
            return cached as Data
        }
        
        // Load from persistent storage
        guard let data = persistent.get(key: key) else { return nil }
        
        // Store in cache
        cache.setObject(data as NSData, forKey: key as NSString)
        return data
    }
    
    func set(key: String, value: Data) {
        cache.setObject(value as NSData, forKey: key as NSString)
        persistent.set(key: key, value: value)
    }
}
```

## Testing

### Mock Request Handler

```swift
class MockRequestHandler: RequestHandler {
    private let responses: [String: Any]
    
    init(responses: [String: Any]) {
        self.responses = responses
    }
    
    func handleRequest(_ request: DataRequest) async throws -> Data {
        let key = "\(request.method)_\(request.params.description)"
        
        guard let response = responses[key] else {
            throw ColibriError.networkError("Mock response not found for: \(key)")
        }
        
        return try JSONSerialization.data(withJSONObject: response)
    }
}

// Test with mock data
let mockHandler = MockRequestHandler(responses: [
    "eth_getBalance_[\"0x742d35Cc...\", \"latest\"]": "0x1bc16d674ec80000"
])

colibri.requestHandler = mockHandler
```

### Test Utils

```swift
class ColibriTestUtils {
    static func createTestColibri(chainId: UInt64 = 1) -> Colibri {
        let colibri = Colibri()
        colibri.chainId = chainId
        colibri.proovers = []  // Force local proof generation
        return colibri
    }
    
    static func createMockStorage() -> ColibriStorage {
        return MockStorage()
    }
    
    static func loadTestData(from file: String) throws -> [String: Any] {
        guard let url = Bundle.module.url(forResource: file, withExtension: "json") else {
            throw ColibriError.invalidParams("Test file not found: \(file)")
        }
        
        let data = try Data(contentsOf: url)
        return try JSONSerialization.jsonObject(with: data) as? [String: Any] ?? [:]
    }
}
```

## CI/CD Integration

### GitHub Actions

The Swift bindings are fully integrated into the CI/CD pipeline:

```yaml
- name: Build iOS XCFramework
  run: |
    cd bindings/swift
    ./build_ios.sh

- name: Test Swift Package (macOS)  
  run: |
    cd bindings/swift
    ./build_macos.sh -dev
    swift test

- name: Test iOS Integration Example
  run: |
    cd test_ios_app
    swift build
    swift test
```

### Automatic Distribution

On every release, a distribution package is automatically created:

1. **iOS XCFramework** for Device + Simulator
2. **Swift Package** with binaryTarget
3. **Upload** to separate distribution repository
4. **Release** on GitHub with versioning

## Troubleshooting

### Common Issues

#### "No such module 'Colibri'"

```bash
# Solution: Check package dependencies
swift package resolve
swift build
```

#### iOS Simulator Crashes

```bash
# Solution: Check architecture
file build_ios_arm/c4_swift.xcframework/ios-x86_64-simulator/c4_swift.framework/c4_swift
# Should show: x86_64 architecture

lipo -info build_ios_arm/c4_swift.xcframework/ios-arm64/c4_swift.framework/c4_swift
# Should show: arm64 architecture
```

#### RPC Calls Fail

```swift
do {
    let result = try await colibri.rpc(method: "eth_blockNumber", params: [])
} catch ColibriError.proofError(let message) {
    print("Proof verification failed: \(message)")
    // In CI/test environments this is normal (missing blockchain state)
} catch {
    print("Unexpected error: \(error)")
}
```

#### Storage Permissions

```swift
// iOS: Consider app sandbox
class DocumentsStorage: ColibriStorage {
    private let documentsDir = FileManager.default.urls(for: .documentDirectory, 
                                                        in: .userDomainMask).first!
    
    func get(key: String) -> Data? {
        let url = documentsDir.appendingPathComponent(key)
        return try? Data(contentsOf: url)
    }
    
    // ... rest of implementation
}
```

### Debug Tips

#### Storage Debug

```swift
class DebugStorage: ColibriStorage {
    private let underlying: ColibriStorage
    
    init(wrapping: ColibriStorage) {
        self.underlying = wrapping
    }
    
    func get(key: String) -> Data? {
        let result = underlying.get(key: key)
        print("🗄️ Storage GET: \(key) → \(result?.count ?? 0) bytes")
        return result
    }
    
    func set(key: String, value: Data) {
        print("🗄️ Storage SET: \(key) ← \(value.count) bytes")
        underlying.set(key: key, value: value)
    }
}
```

#### Network Debug

```swift
class DebugRequestHandler: RequestHandler {
    func handleRequest(_ request: DataRequest) async throws -> Data {
        print("🌐 Request: \(request.method) \(request.params)")
        let start = Date()
        
        let result = try await originalHandler.handleRequest(request)
        
        let duration = Date().timeIntervalSince(start) * 1000
        print("🌐 Response: \(result.count) bytes in \(duration)ms")
        
        return result
    }
}
```

## Platform Specifics

### iOS Considerations

- **App Transport Security**: HTTPS required for all prover URLs
- **Background Tasks**: RPC calls in background apps may be terminated
- **Memory Limits**: Adjust storage cache size to iOS memory limits
- **Network Reachability**: Offline capabilities through local proof generation

### macOS Considerations

- **Sandboxing**: Configure file system access for storage
- **Code Signing**: All C libraries must be signed
- **Rosetta**: Intel Mac compatibility through Universal Binaries

## Further Information

- **📖 Online Documentation**: [GitBook Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/swift)
- **Core Repository**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Distribution Package**: [colibri-stateless-swift](https://github.com/corpus-core/colibri-stateless-swift)
- **Example iOS App**: `bindings/swift/test_ios_app/`
- **Integration Tests**: `bindings/swift/Tests/GeneratedIntegrationTests.swift`