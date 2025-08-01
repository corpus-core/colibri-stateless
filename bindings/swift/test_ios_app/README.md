# Colibri iOS Test App

This iOS test application demonstrates how to integrate and use the **Colibri Stateless Swift Package** in your iOS project. It serves two purposes:

1. **🧪 CI Integration Testing** - Verifies the package works correctly in iOS environments
2. **📖 Developer Reference** - Shows best practices for integrating Colibri in your app

## 🏗️ Architecture

```
ColibriTestApp/
├── Package.swift              # Swift Package configuration
├── Sources/
│   └── ColibriTestApp/
│       └── main.swift         # Demo implementation
├── Tests/
│   └── ColibriTestAppTests/
│       └── ColibriIntegrationTests.swift  # Integration tests
└── README.md                  # This file
```

## 🚀 Quick Start

### Local Testing (Recommended)

1. **Build the iOS Package:**
   ```bash
   cd bindings/swift
   ./build_ios.sh                    # Creates ios_package/ with XCFramework
   ```

2. **Test the Integration:**
   ```bash
   cd test_ios_app
   swift build                       # Build for iOS Simulator
   swift test                        # Run integration tests
   ```

### For iOS App Developers

Replace the local dependency in `Package.swift` with the published package:

```swift
dependencies: [
    .package(url: "https://github.com/corpus-core/colibri-stateless-swift.git", from: "1.0.0")
]
```

### For Local Development

First, build the iOS package:

```bash
cd bindings/swift
./build_ios.sh                # Creates ios_package/
```

The test app uses a local reference to the built package:

```swift
dependencies: [
    .package(path: "../ios_package")  // Points to locally-built iOS package
]
```

### For CI/Testing

In CI, the package is built in the same location and tested directly without any copying or renaming.

## 📱 Integration Examples

### 1. Basic Setup

```swift
import Colibri

let colibri = Colibri()
colibri.chainId = 1  // Ethereum Mainnet
colibri.proofers = ["https://c4.incubed.net"]  // Remote proofer
```

### 2. Method Support Check

```swift
let isSupported = colibri.getMethodSupport(method: "eth_getBalance")
if isSupported {
    // Method is available
}
```

### 3. Making RPC Calls

```swift
do {
    let result = try await colibri.rpc(method: "eth_blockNumber", params: [])
    if let blockNumber = result as? String {
        print("Current block: \(blockNumber)")
    }
} catch {
    print("RPC call failed: \(error)")
}
```

### 4. Custom Storage Implementation

```swift
class MyAppStorage: ColibriStorage {
    func get(key: String) -> Data? {
        // Your storage implementation
        return UserDefaults.standard.data(forKey: key)
    }
    
    func set(key: String, value: Data) {
        UserDefaults.standard.set(value, forKey: key)
    }
    
    func delete(key: String) {
        UserDefaults.standard.removeObject(forKey: key)
    }
}

// Register your storage
StorageBridge.registerStorage(MyAppStorage())
```

## 🧪 Running Tests

### Local Development

```bash
cd test_ios_app
swift test
```

### iOS Simulator (Xcode)

1. Open `Package.swift` in Xcode
2. Select iOS Simulator target
3. Run tests with `Cmd+U`

## 🔧 Supported Platforms

- **iOS 13.0+** (Device + Simulator)
- **macOS 10.15+** (for local development)

## 📊 Test Coverage

The integration tests cover:

- ✅ **Initialization** - Basic setup and configuration
- ✅ **Method Support** - Checking available RPC methods
- ✅ **Storage System** - Custom storage implementations
- ✅ **Error Handling** - Proper error propagation
- ✅ **Performance** - Memory usage and initialization speed
- ✅ **Platform Compatibility** - iOS/macOS specific checks

## 🚧 CI Integration

This test app is automatically built and tested in GitHub Actions:

```yaml
- name: Test iOS Integration
  run: |
    cd test_ios_app
    swift build
    swift test
```

## 💡 Best Practices

### 1. Error Handling

Always wrap RPC calls in do-catch blocks:

```swift
do {
    let result = try await colibri.rpc(method: "eth_call", params: params)
    // Handle success
} catch {
    // Handle error appropriately for your UI
    print("Blockchain call failed: \(error.localizedDescription)")
}
```

### 2. Storage Management

Implement storage that persists across app launches:

```swift
class PersistentStorage: ColibriStorage {
    private let documentsDirectory = FileManager.default.urls(for: .documentDirectory, 
                                                             in: .userDomainMask).first!
    
    func get(key: String) -> Data? {
        let url = documentsDirectory.appendingPathComponent(key)
        return try? Data(contentsOf: url)
    }
    
    func set(key: String, value: Data) {
        let url = documentsDirectory.appendingPathComponent(key)
        try? value.write(to: url)
    }
    
    func delete(key: String) {
        let url = documentsDirectory.appendingPathComponent(key)
        try? FileManager.default.removeItem(at: url)
    }
}
```

### 3. Chain Configuration

Configure chains based on your app's needs:

```swift
enum SupportedChain {
    case ethereum, polygon, arbitrum, base
    
    var chainId: UInt64 {
        switch self {
        case .ethereum: return 1
        case .polygon: return 137
        case .arbitrum: return 42161
        case .base: return 8453
        }
    }
}

colibri.chainId = SupportedChain.ethereum.chainId
```

## 🐛 Troubleshooting

### Build Issues

1. **"No such module 'Colibri'"**
   - Ensure the package dependency is correctly configured
   - Clean build folder: `swift package clean`

2. **iOS Simulator crashes**
   - Check iOS deployment target (minimum iOS 13.0)
   - Verify XCFramework architecture matches simulator

3. **RPC calls fail**
   - In CI/test environments, this is expected due to missing blockchain state
   - Use try-catch blocks to handle failures gracefully

### Performance Issues

1. **Slow initialization**
   - Initialize Colibri once and reuse the instance
   - Consider background initialization

2. **Memory usage**
   - Implement proper storage cleanup
   - Monitor memory usage in Instruments

## 📚 Additional Resources

- **Full Documentation**: See `doc.md` for detailed API reference
- **Swift Package**: [colibri-stateless-swift](https://github.com/corpus-core/colibri-stateless-swift)
- **Main Repository**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)

## 🤝 Contributing

Found an issue or want to improve the example? 

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

This test app is meant to be a living example - keep it updated as the API evolves!