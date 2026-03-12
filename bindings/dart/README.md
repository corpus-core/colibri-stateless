<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="C4 Logo" width="300"/>

# Colibri Dart Bindings

Dart FFI bindings for Colibri Stateless (proof generation + verification).

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

## Features

- Proof generation + verification through the C core
- Async request handling with `dart:async`
- Multi-chain configuration with sensible defaults

You can also set the library path via `COLIBRI_DART_LIBRARY`.

### Web (WASM)

When compiled for **web**, the package uses the JavaScript/WASM Colibri build instead of FFI. Load the Colibri WASM bundle and `colibri_web_bridge.js` (see **colibri_flutter** README for web setup), then use the same API: `Colibri(chainId: 1)` — no `libraryPath` on web.

### Flutter (with bundled binaries)

For Flutter apps, use the **colibri_flutter** package on pub.dev — it includes Android and iOS binaries, so no separate build is required:

```yaml
dependencies:
  colibri_flutter: ^0.1.7
```

```dart
import 'package:colibri_flutter/colibri_flutter.dart';
final colibri = Colibri(chainId: 1);
```

See [flutter/colibri_flutter/README.md](flutter/colibri_flutter/README.md). On Android and iOS, if you omit `storage`, the client uses in-memory storage by default (see [Colibri](lib/src/client.dart) constructor docs).

### iOS (building yourself)

iOS does not allow dynamic `dlopen` of external libraries. When building yourself, place the XCFramework at `bindings/dart/flutter/colibri_flutter/ios/Frameworks/c4_swift.xcframework`, or use **colibri_flutter** from pub.dev (see above).

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
- iOS: `bindings/dart/flutter/colibri_flutter/ios/Frameworks/c4_swift.xcframework` (macOS only)
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
