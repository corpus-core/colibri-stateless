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

## Build (Debug)

```bash
./build_debug.sh
```

## Testing

```bash
dart test
```

If the native library lives elsewhere, set `COLIBRI_DART_LIBRARY`.

## Documentation

- Core docs: https://corpus-core.gitbook.io/specification-colibri-stateless
- Bindings overview: https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings
