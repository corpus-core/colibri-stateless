# Swift Bindings for Colibri

The Colibri bindings for Swift are built using CMake and Swift Package Manager. It can be used in iOS (13.0+) and macOS (10.15+) applications.

## Usage

Add Colibri to your `Package.swift`:

```swift
// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "YourApp",
    platforms: [
        .iOS(.v13),
        .macOS(.v10_15)
    ],
    dependencies: [
        .package(name: "Colibri", path: "path/to/colibri/bindings/swift")
    ],
    targets: [
        .target(
            name: "YourApp",
            dependencies: ["Colibri"]
        )
    ]
)
```

Use it in your code:

```swift
import Colibri

class ExampleViewController: UIViewController {
    let proofManager = ColibriProofManager()
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        // Configure the proof manager
        proofManager.eth_rpcs = ["https://eth-mainnet.g.alchemy.com/v2/YOUR-API-KEY"]
        proofManager.beacon_apis = ["https://beacon.quicknode.com/YOUR-API-KEY"]
        proofManager.chainId = 1  // Ethereum Mainnet
        
        // Create and verify a proof
        Task {
            do {
                let method = "eth_getBalance"
                let params = """
                {
                    "address": "0x742d35Cc6634C0532925a3b844Bc454e4438f44e",
                    "block": "latest"
                }
                """
                
                // Create proof
                let proof = try await proofManager.createProof(method: method, params: params)
                print("Proof created successfully! Length: \(proof.count)")
                
                // Verify proof
                let result = try await proofManager.verifyProof(proof: proof, method: method, params: params)
                print("Verification result: \(result)")
            } catch {
                print("Error: \(error)")
            }
        }
    }
}
```

## Building

### Prerequisites
- Xcode 12.0 or later
- CMake 3.10 or later
- iOS SDK 13.0 or later

### Building the XCFramework

The XCFramework contains both simulator (x86_64) and device (arm64) architectures. Build it with:

```bash
# Build for x86_64 (simulator)
cmake -DSWIFT=true -B build_x86 -DCMAKE_OSX_ARCHITECTURES=x86_64 ..
cd build_x86
make

# Build for arm64 (device)
cd ..
cmake -DSWIFT=true -DCMAKE_OSX_ARCHITECTURES=arm64 -DSWIFT_X86_BUILD=$(pwd)/build_x86 -B build ..
cd build
make
```

This will create `c4_swift.xcframework` in the build directory, which contains both architectures.

### Running Tests

After building the XCFramework, you can run the tests:

```bash
cd bindings/swift
swift test
```

## Features

- Async/await support for modern Swift concurrency
- Native Swift types and error handling
- Support for both iOS and macOS platforms
- Comprehensive test suite
- Thread-safe proof creation and verification

## Error Handling

The framework uses Swift's native error handling. Possible errors include:

```swift
public enum ColibriError: Error {
    case invalidInput
    case executionFailed
    case invalidJSON
    case proofError(String)
    case unknownStatus(String)
    case invalidURL
}
```

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request 