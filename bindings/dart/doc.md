: Bindings

:: Dart

Dart FFI bindings for the Colibri stateless Ethereum proof library. Generate and verify cryptographic proofs for Ethereum RPC calls from Dart applications (CLI, servers, or desktop).

## Overview

The Colibri Dart bindings provide an async Dart API that calls the Colibri C core via FFI. They are used both as a standalone package (`colibri_stateless`) and as the runtime behind the Flutter plugin (`colibri_flutter`). All RPC responses can be validated with Merkle proofs; ZK proofs are requested from remote provers when configured.

### Core Features

- **Cryptographic verification** – RPC results verified with Merkle (and optionally ZK) proofs
- **Async/await** – `Future`-based API for RPC and proof operations
- **Pluggable storage** – Implement [ColibriStorage] for custom cache backends
- **Multi-chain** – Configurable chain ID, provers, eth Rpcs, beacon APIs
- **No Flutter dependency** – Pure Dart; use from CLI or server

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Dart Application Layer                       │
├─────────────────────────────────────────────────────────────────┤
│                   package:colibri_stateless                      │
│  • Colibri class (rpc, createProof, verifyProof)                │
│  • ColibriStorage interface                                      │
│  • Error types (ColibriError, ProofError, RPCError, …)           │
├─────────────────────────────────────────────────────────────────┤
│                      Dart FFI Layer                              │
│  • native/ (libcolibri.dylib / .so / .dll)                       │
│  • ColibriNative (load, registerStorage, createProverCtx, …)     │
├─────────────────────────────────────────────────────────────────┤
│                      Core C Libraries                            │
│  • Prover, Verifier, storage plugin system                       │
└─────────────────────────────────────────────────────────────────┘
```

## Installation

### From pub.dev (colibri_stateless)

```yaml
dependencies:
  colibri_stateless: ^0.1.5
```

You must build or provide the native library separately (see Building from source). Alternatively, use **colibri_flutter** for mobile/desktop with bundled binaries.

### Development / from source

```bash
git clone https://github.com/corpus-core/colibri-stateless.git
cd colibri-stateless/bindings/dart

./build.sh
```

This produces a shared library in `native/` (e.g. `native/libcolibri.dylib` on macOS). Set `COLIBRI_DART_LIBRARY` to its path, or pass `libraryPath` to the [Colibri] constructor.

## Quick Start

### Basic RPC

```dart
import 'package:colibri_stateless/colibri_stateless.dart';

Future<void> main() async {
  final colibri = Colibri(
    chainId: 1,
    libraryPath: 'native/libcolibri.dylib', // or set COLIBRI_DART_LIBRARY
  );

  final blockNumber = await colibri.rpc('eth_blockNumber', []);
  print('Block number: $blockNumber');

  colibri.close();
}
```

### Local proof generation

Use empty `provers` and provide `ethRpcs` and `beaconApis` so the client can generate proofs locally:

```dart
final colibri = Colibri(
  chainId: 1,
  provers: [],
  ethRpcs: ['https://eth.llamarpc.com'],
  beaconApis: ['https://lodestar-mainnet.chainsafe.io'],
  libraryPath: 'native/libcolibri.dylib',
);

final result = await colibri.rpc('eth_getProof', [
  '0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5',
  ['0x0'],
  'latest',
]);
colibri.close();
```

## API Reference

### Colibri class

```dart
class Colibri {
  Colibri({
    this.chainId = 1,
    List<String>? provers,
    List<String>? ethRpcs,
    List<String>? beaconApis,
    List<String>? checkpointz,
    this.trustedCheckpoint,
    this.includeCode = false,
    this.zkProof = false,
    this.checkpointWitnessKeys,
    this.storage,
    void Function(String message)? onDebug,
    String? libraryPath,
    http.Client? httpClient,
  });

  Future<dynamic> rpc(String method, List<dynamic> params);
  Future<Uint8List> createProof(String method, List<dynamic> params);
  Future<dynamic> verifyProof(Uint8List proof);
  MethodType getMethodSupport(String method);
  void close();
}
```

- **rpc** – Executes an RPC call with proof generation and verification (remote or local). Returns the verified result.
- **createProof** – Builds a proof locally (Merkle only; `zkProof` is ignored for local creation).
- **verifyProof** – Verifies serialized proof bytes and returns the decoded result.
- **getMethodSupport** – Returns whether the method is supported locally, only remotely, or not at all.
- **close** – Releases native resources; call when done.

Constructor: **libraryPath** overrides the default native library (or use env `COLIBRI_DART_LIBRARY`). **storage** registers a custom cache; **zkProof** requests ZK proofs from remote provers when using **rpc** with provers. **onDebug** can contain sensitive data; do not forward to production logging.

### Storage

```dart
abstract class ColibriStorage {
  Uint8List? get(String key);
  void set(String key, Uint8List value);
  void delete(String key);
  List<String> listKeys();
}
```

Implement this to provide a custom cache backend. Default on desktop is native file storage; on Android/iOS the client uses [MemoryStorage] if no storage is given (native file storage is not used on mobile).

## Configuration

### Chain and endpoints

```dart
final colibri = Colibri(
  chainId: 1,  // Ethereum mainnet; 11155111 = Sepolia, 137 = Polygon, etc.
  provers: ['https://mainnet.colibri-proof.tech'],
  ethRpcs: ['https://eth.llamarpc.com', 'https://rpc.ankr.com/eth'],
  beaconApis: ['https://lodestar-mainnet.chainsafe.io'],
  trustedCheckpoint: '0x…',  // optional
  libraryPath: 'native/libcolibri.dylib',
);
```

### Environment

- **COLIBRI_DART_LIBRARY** – Path to the native shared library if not passing **libraryPath**.

## Error handling

```dart
import 'package:colibri_stateless/colibri_stateless.dart';

try {
  final result = await colibri.rpc('eth_getBalance', ['0x…', 'latest']);
} on ProofError catch (e) {
  // Proof generation or verification failed
} on RPCError catch (e) {
  // RPC or network error
} on ColibriError catch (e) {
  // Other Colibri errors
}
```

## Building from source

```bash
cd bindings/dart
./build.sh          # Release
./build_debug.sh    # Debug with symbols
```

Native library output:

- macOS: `native/libcolibri.dylib`
- Linux: `native/libcolibri.so`
- Windows: `native/colibri.dll`

## Testing

```bash
dart test
# or
./run_tests.sh
```

Set `COLIBRI_DART_LIBRARY` if the library is not in the default path. Coverage: `./test/run_coverage.sh` (output in `test/coverage/`).

## Further information

- **Specification**: [GitBook – Bindings](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings)
- **Repository**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Examples**: `example/` (basic_usage, proof_verify, custom_storage, etc.; see `example/README.md`)

---

:: Flutter

Flutter plugin for Colibri Stateless with **bundled native binaries** for Android, iOS, macOS, Linux (and Windows when built on Windows). No manual build or `libraryPath` required on mobile; on desktop you pass the plugin’s bundled library path.

## Overview

**colibri_flutter** is the same Colibri Dart API as **colibri_stateless**, packaged as a Flutter plugin with pre-built native libraries. Use it in Flutter apps when you want verified RPC without managing the native build yourself. Flutter web is not supported (no FFI).

### When to use

- **Mobile (Android / iOS)** – Use **colibri_flutter** from pub.dev; binaries are included.
- **Desktop (macOS / Linux / Windows)** – Use **colibri_flutter** and pass `libraryPath: colibriFlutterLibraryPath` after building desktop binaries (or use a published package that ships them).

## Installation

### pub.dev (recommended for mobile)

```yaml
dependencies:
  colibri_flutter: ^0.1.5
```

```bash
flutter pub get
```

### Path dependency (development)

```yaml
dependencies:
  colibri_flutter:
    path: /path/to/colibri-stateless/bindings/dart/flutter/colibri_flutter
  colibri_stateless:
    path: /path/to/colibri-stateless/bindings/dart
```

## Usage

```dart
import 'package:colibri_flutter/colibri_flutter.dart';

// Android/iOS: libraryPath not needed (plugin loads the lib).
// macOS/Linux: use bundled lib path.
final colibri = Colibri(chainId: 1, libraryPath: colibriFlutterLibraryPath);

final blockNumber = await colibri.rpc('eth_blockNumber', []);
colibri.close();
```

- On **Android** and **iOS**, the plugin loads the native library automatically; **colibriFlutterLibraryPath** is `null` and can be omitted.
- On **macOS** and **Linux**, pass **libraryPath: colibriFlutterLibraryPath** so the app uses the plugin’s bundled library (you must have built desktop binaries for it to be present).

## Platform notes

- **Android** – `libcolibri.so` is loaded from the plugin’s `jniLibs` when the engine attaches.
- **iOS** – The XCFramework is linked; no path needed. When building yourself, place it under `ios/Frameworks/` or use the published package.
- **Desktop** – Build binaries from the repo root with `./scripts/build_flutter_binaries.sh --macos` (or `--linux`, `--windows` as appropriate). Requires Android NDK for Android, Xcode on macOS for iOS.
- **Storage** – On Android and iOS, if you omit **storage**, the client uses in-memory storage by default (native file storage is not used on mobile).

## Building native binaries (maintainers)

From the **repository root**:

```bash
./scripts/build_flutter_binaries.sh
# or per platform:
./scripts/build_flutter_binaries.sh --android
./scripts/build_flutter_binaries.sh --ios
./scripts/build_flutter_binaries.sh --macos
./scripts/build_flutter_binaries.sh --linux
./scripts/build_flutter_binaries.sh --windows
```

Output: Android `jniLibs`, iOS XCFramework, and desktop libraries as documented in the main Dart README.

## Further information

- **Plugin README**: [flutter/colibri_flutter/README.md](https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/dart/flutter/colibri_flutter/README.md)
- **Specification**: [GitBook – Bindings](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings)
- **Pub package**: [colibri_flutter](https://pub.dev/packages/colibri_flutter)
