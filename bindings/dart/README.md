<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="Colibri Logo" width="300"/>

# Colibri Stateless — Dart / Flutter

Dart FFI bindings for **Colibri v2** (package line `0.2.x`) — proof generation and verification.

Current package versions:

| Package | Version |
|---------|---------|
| [`colibri_stateless`](pubspec.yaml) | **0.2.0** |
| [`colibri_flutter`](flutter/colibri_flutter/pubspec.yaml) | **0.2.1** |

**Verify Ethereum RPC data cryptographically — without running a full node.**

![ETH2.0 Spec Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
[![pub](https://img.shields.io/pub/v/colibri_flutter.svg)](https://pub.dev/packages/colibri_flutter)

Colibri Stateless is a highly efficient prover/verifier for Ethereum (with upcoming support for Layer-2s such as OP-Stack). These Dart FFI bindings wrap the C core and verify every RPC response against the beacon chain — no full node, no continuous sync. Use them standalone (`colibri_stateless`) or via the Flutter plugin (`colibri_flutter`) with bundled native binaries.

[**Website**](https://www.corpuscore.tech/colibri) · [**Docs**](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/dart) · [**Whitepaper**](https://corpus-core.gitbook.io/whitepaper-colibri-stateless) · [**Privacy (PAP)**](https://corpus-core.gitbook.io/pap-colibri-stateless)

## Why Colibri?

- **Stateless** — verification needs nothing but the proof and the sync committee it is checked against. The committee is cached locally so it does not have to travel with every request, but it works just as well with an empty cache or none at all. No persistent state, no full node.
- **Cryptographically verified RPC** — every RPC response is checked against BLS signatures.
- **Offline verification** — proofs are fully self-contained and verify without any network connection, thanks to zk-proofs for the sync committee and signed checkpoints.
- **On-demand, not always-on** — work happens only when you make a request; no background sync burning bandwidth, CPU, or battery.
- **Verifies historical data (older than ~27h / 8192 blocks)** — via `historical_summaries` proofs, where other light clients simply fail.
- **`eth_getLogs` completeness proofs** — proves no matching log was omitted in the requested range.
- **Fully verified local transaction simulation** — simulate a transaction against verified state before signing.
- **Privacy-aware** — Pragmatic Adaptive Privacy (PAP) mode. *Experimental.*

## Quick Start

### Build the native library

```bash
./build.sh
```

This produces a shared library in `native/`:

- macOS: `native/libcolibri.dylib`
- Linux: `native/libcolibri.so`
- Windows: `native/colibri.dll`

### Use in Dart

```dart
import 'package:colibri_stateless/colibri.dart';

Future<void> main() async {
final colibri = Colibri(
  chainId: 1,
  libraryPath: 'native/libcolibri.dylib', // adjust for your OS
);

  final blockNumber = await colibri.rpc('eth_blockNumber', []);
  print('Block number: $blockNumber');

  colibri.close();
}
```

## Dart-specific features

- Async request handling with `dart:async`
- Multi-chain configuration with sensible defaults
- Flutter plugin (`colibri_flutter`) with bundled Android/iOS/desktop binaries
- **Local `eth_call` proofs** – `useAccesslist: true` (default) uses `eth_createAccessList`; set `false` for legacy `debug_traceCall`
- **Privacy-preserving `eth_call`** – `proverMode: hybrid`, `privacyMode: basic`, `obliviousNodes` (default empty; e.g. `https://rpc.safe-node.com/`, API key for testing). Setting `obliviousNodes` auto-enables PAP. TEE/ORAM: [Oblivious Labs](https://www.obliviouslabs.com/).

You can also set the library path via `COLIBRI_DART_LIBRARY`.

### Flutter (with bundled binaries)

For Flutter apps, use the **colibri_flutter** package on pub.dev — it includes Android and iOS binaries, so no separate build is required:

```yaml
dependencies:
  colibri_flutter: ^0.2.1
```

```dart
import 'package:colibri_flutter/colibri_flutter.dart';
final colibri = Colibri(chainId: 1);
```

See [flutter/colibri_flutter/README.md](flutter/colibri_flutter/README.md). On Android and iOS, if you omit `storage`, the client uses in-memory storage by default (see [Colibri](lib/src/client.dart) constructor docs).

### iOS (building yourself)

iOS does not allow dynamic `dlopen` of external libraries. When building yourself, place the XCFramework at `bindings/dart/flutter/colibri_flutter/ios/colibri_flutter/Frameworks/c4_swift.xcframework`, or use **colibri_flutter** from pub.dev (see above). FFI symbols are retained via `force_link.c` + `-force_load` (see the Flutter plugin README troubleshooting section).

## Build (Debug)

```bash
./build_debug.sh
```

## Flutter / Mobile Binaries

From the **repository root**, one command builds platform-specific binaries for Flutter:

```bash
./scripts/build_flutter_binaries.sh
```

This produces:

- Android: `bindings/dart/flutter/colibri_flutter/android/src/main/jniLibs/<abi>/libcolibri.so`
- iOS: `bindings/dart/flutter/colibri_flutter/ios/colibri_flutter/Frameworks/c4_swift.xcframework` (macOS only)
- Windows: `bindings/dart/native/windows/colibri.dll` (Windows host only)

Requirements:

- Android: `ANDROID_NDK_HOME` (or `ANDROID_NDK`) set
- iOS: macOS + Xcode (uses `bindings/swift/build_ios.sh`)
- Windows: build on a Windows host

The build script uses `-DETH_ZKPROOF=ON` when building the C core for ZK proof support.

Per-platform:

```bash
./scripts/build_flutter_binaries.sh --android
./scripts/build_flutter_binaries.sh --ios
./scripts/build_flutter_binaries.sh --macos    # macOS host; universal dylib
./scripts/build_flutter_binaries.sh --linux    # Linux host
./scripts/build_flutter_binaries.sh --windows
```

## Examples

- `example/basic_usage.dart` — minimal verified RPC call
- `example/proof_verify.dart` — create + verify a proof manually
- `example/custom_storage.dart` — custom storage integration
- `example/unproofable_rpc.dart` — unproofable method routed to direct RPC
- `example/read_block.dart`, `example/read_logs.dart`, `example/contract_call.dart`, `example/transaction_receipt.dart` — more RPC examples

See [example/README.md](example/README.md) for run commands and optional `.env` configuration.

## Testing

```bash
dart test
```

Alternatively: `./run_tests.sh` (handles native library path etc.). If the native library lives elsewhere, set `COLIBRI_DART_LIBRARY`.

### Coverage

```bash
./test/run_coverage.sh
```

Coverage output is written to `test/coverage/` (LCOV file: `test/coverage/lcov.info`).

### Compare C vs Dart results

From the **repository root** (requires C build):

```bash
export C4_BUILD_DIR=/path/to/cmake/build
./scripts/compare_c_dart_tests.sh
```

## Publishing (pub.dev)

Package version is synced with the repository root. The canonical version is in the repo root file **`VERSION`** (same as used for releases). To update the Dart/Flutter package versions from it:

```bash
./scripts/sync_version.sh
```

Then publish **colibri_stateless** (from a copy that excludes the Flutter plugin, so the package stays small):

```bash
./scripts/publish_colibri_stateless.sh --dry-run   # check
./scripts/publish_colibri_stateless.sh             # publish
```

To publish **colibri_flutter** (builds native binaries, then publishes):

```bash
cd flutter/colibri_flutter
./scripts/publish_colibri_flutter.sh --dry-run   # build + check
./scripts/publish_colibri_flutter.sh             # build + publish
```

This script builds the Android `.so` files (requires `ANDROID_NDK_HOME`) and the iOS XCFramework (requires macOS + Xcode), then runs `dart pub publish`. The binaries are **not** checked into git; they are built on demand before each publish.

Alternatively, build binaries separately and publish manually:

```bash
./scripts/build_native_libs.sh --all   # from flutter/colibri_flutter/
dart pub publish
```

Or use the repo-root script which also copies binaries into the plugin:

```bash
# from repo root
./scripts/build_flutter_binaries.sh --android --ios
cd bindings/dart/flutter/colibri_flutter
dart pub publish
```

You will be prompted to log in with a Google account (first time: create publisher or link account at [pub.dev](https://pub.dev)).

Recommended: publish from a clean git state and after running `dart test`.

## Documentation

- Core docs: https://corpus-core.gitbook.io/specification-colibri-stateless
- Bindings overview: https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings
