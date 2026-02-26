# Colibri Flutter

Flutter plugin for Colibri Stateless with **bundled native binaries** (Android + iOS). No manual build or `libraryPath` needed.

## Install (pub.dev)

```yaml
dependencies:
  colibri_flutter: ^0.1.0
```

Then:

```bash
flutter pub get
```

## Usage

```dart
import 'package:colibri_flutter/colibri_flutter.dart';

final colibri = Colibri(chainId: 1);
final block = await colibri.rpc('eth_blockNumber', []);
colibri.close();
```

On Android and iOS the native library is loaded automatically (no `libraryPath`).

## Local / path dependency

For development against a local `colibri_stateless`:

```yaml
dependencies:
  colibri_flutter:
    path: /path/to/colibri-stateless/bindings/dart/flutter/colibri_flutter
  colibri_stateless:
    path: /path/to/colibri-stateless/bindings/dart
```

## Building native binaries (for maintainers)

To refresh the binaries shipped in this package:

```bash
# from repo root
./scripts/build_flutter_binaries.sh
```

This updates:

- `android/src/main/jniLibs/<abi>/libcolibri.so`
- `ios/Frameworks/c4_swift.xcframework`

## Publishing (pub.dev)

1. Publish **colibri_stateless** first from `bindings/dart`.
2. **Repository verification:** pub.dev expects the [repository](pubspec.yaml) URL to clone to a repo that contains a `pubspec.yaml` with `name: colibri_flutter` at root. This package lives in a monorepo subdirectory, so to pass that check either:
   - **Option A:** Create a mirror repo (e.g. `corpus-core/colibri-flutter`) with this directory’s contents at root. Push the mirror, set `repository: https://github.com/corpus-core/colibri-flutter` in [pubspec.yaml](pubspec.yaml), then publish.
   - **Option B:** Run `./scripts/prepare_pub_mirror.sh` from this directory to copy the package into a sibling folder; push that folder as the mirror repo, then set `repository` in pubspec to the mirror URL and publish.
3. From this directory: `dart pub publish --dry-run`, then `dart pub publish`.

## iOS note

iOS does not allow dynamic `dlopen` of external libraries. The XCFramework is linked into the app; the Dart FFI loader uses `DynamicLibrary.process()`.
