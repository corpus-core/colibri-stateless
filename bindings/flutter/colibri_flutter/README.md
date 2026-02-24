# Colibri Flutter

Flutter wrapper for the Colibri Dart FFI bindings.

## Install (local path)

```yaml
dependencies:
  colibri_flutter:
    path: ../colibri-stateless/bindings/flutter/colibri_flutter
```

## Native binaries

Build and copy all native binaries:

```bash
./scripts/build_flutter_binaries.sh
```

This script will place binaries into:

- `bindings/flutter/colibri_flutter/android/src/main/jniLibs/<abi>/libcolibri.so`
- `bindings/flutter/colibri_flutter/ios/Frameworks/c4_swift.xcframework`

## Usage

```dart
import 'package:colibri_flutter/colibri_flutter.dart';

final colibri = Colibri(chainId: 1);
final block = await colibri.rpc('eth_blockNumber', []);
```

## iOS note

iOS does not allow dynamic `dlopen` of external libraries. The XCFramework is
linked into the app; the Dart FFI loader should use `DynamicLibrary.process()`.
