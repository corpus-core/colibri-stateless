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

### iOS (Flutter)

iOS loads the native library via `DynamicLibrary.process()` (no `dlopen`).
Use the Flutter wrapper package in `bindings/dart/flutter/colibri_flutter`, and
copy the XCFramework into:

```
bindings/dart/flutter/colibri_flutter/ios/Frameworks/c4_swift.xcframework
```

## Build (Debug)

```bash
./build_debug.sh
```

## Flutter / Mobile Binaries

One command to build platform-specific binaries for Flutter:

```bash
./scripts/build_flutter_binaries.sh
```

This script produces:

- Android: `bindings/dart/native/android/<abi>/libcolibri.so`
- iOS: `bindings/dart/native/ios/c4_swift.xcframework` (macOS only)
- Windows: `bindings/dart/native/windows/colibri.dll` (Windows host only)

Requirements:

- Android: `ANDROID_NDK_HOME` (or `ANDROID_NDK`) set
- iOS: macOS + Xcode (uses `bindings/swift/build_ios.sh`)
- Windows: build on a Windows host

You can also run per-platform:

```bash
./scripts/build_flutter_binaries.sh --android
./scripts/build_flutter_binaries.sh --ios
./scripts/build_flutter_binaries.sh --windows
```

## Examples

- `example/basic_usage.dart` — minimal verified RPC call
- `example/proof_verify.dart` — create + verify a proof manually
- `example/custom_storage.dart` — custom storage integration
- `example/unproofable_rpc.dart` — unproofable method routed to direct RPC

## Testing

```bash
dart test
```

If the native library lives elsewhere, set `COLIBRI_DART_LIBRARY`.

### Compare C vs Dart results

```bash
export C4_BUILD_DIR=/path/to/cmake/build
./scripts/compare_c_dart_tests.sh
```

## Documentation

- Core docs: https://corpus-core.gitbook.io/specification-colibri-stateless
- Bindings overview: https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings
