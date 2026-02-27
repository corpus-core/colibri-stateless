# Colibri Flutter

Flutter plugin for Colibri Stateless with **bundled native binaries** (Android, iOS, macOS, Linux). No manual build or `libraryPath` needed on mobile; on desktop use `colibriFlutterLibraryPath`.

## Install (pub.dev)

```yaml
dependencies:
  colibri_flutter: ^0.1.2
```

Then:

```bash
flutter pub get
```

## Usage

```dart
import 'package:colibri_flutter/colibri_flutter.dart';

// On Android/iOS: no libraryPath. On macOS/Linux: use bundled lib path.
final colibri = Colibri(chainId: 1, libraryPath: colibriFlutterLibraryPath);
final block = await colibri.rpc('eth_blockNumber', []);
colibri.close();
```

On Android and iOS the native library is loaded automatically; `colibriFlutterLibraryPath` is `null` there. On macOS and Linux, pass `colibriFlutterLibraryPath` so the plugin’s bundled library is used.

**Flutter web is not supported** (no native FFI). Use Android, iOS, macOS, Linux, or Windows.

## Testing the plugin

**1. Run the example app** (from this repo):

```bash
cd bindings/dart/flutter/colibri_flutter/example
flutter pub get
flutter run
# Pick a device (e.g. Android emulator or iOS simulator), then tap "Fetch block number".
```

**2. In your own app**

- Add dependency: `colibri_flutter: ^0.1.2` (and run `flutter pub get`).
- Use the same constructor so the plugin’s library is used on all platforms:

```dart
final colibri = Colibri(chainId: 1, libraryPath: colibriFlutterLibraryPath);
```

- **Android**: The plugin loads `libcolibri.so` from `jniLibs` when the engine attaches. No path needed; `colibriFlutterLibraryPath` is `null`.
- **iOS**: The XCFramework is linked into the app; no path needed.
- **macOS / Linux**: You must pass `colibriFlutterLibraryPath` and have built the desktop binaries (see “Building native binaries” below).

**3. If the app crashes on the first Colibri call**

- **Android**: Ensure the plugin’s `jniLibs` contain `libcolibri.so` for your ABI (arm64-v8a, armeabi-v7a, x86_64). If you depend on the published package, they are included. If you use a path dependency, run `./scripts/build_flutter_binaries.sh --android` from the repo root and use the plugin from there.
- **iOS**: Ensure the XCFramework is present under `ios/Frameworks/` in the plugin (run `build_flutter_binaries.sh --ios` if using a path dependency).
- **Desktop**: Pass `libraryPath: colibriFlutterLibraryPath` and ensure you have run the build script for macOS/Linux so the bundled lib exists.

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
# Or per platform:
./scripts/build_flutter_binaries.sh --android
./scripts/build_flutter_binaries.sh --ios
./scripts/build_flutter_binaries.sh --macos   # macOS only (universal arm64 + x86_64)
./scripts/build_flutter_binaries.sh --linux   # Linux host only
./scripts/build_flutter_binaries.sh --windows
```

This updates:

- `android/src/main/jniLibs/<abi>/libcolibri.so`
- `ios/Frameworks/c4_swift.xcframework`
- `macos/Frameworks/libcolibri.dylib` (universal)
- `linux/lib/libcolibri.so`

## Publishing (pub.dev)

1. Publish **colibri_stateless** first from `bindings/dart`.
2. **Repository verification:** pub.dev expects the [repository](pubspec.yaml) URL to clone to a repo that contains a `pubspec.yaml` with `name: colibri_flutter` at root. This package lives in a monorepo subdirectory, so to pass that check either:
   - **Option A:** Create a mirror repo (e.g. `corpus-core/colibri-flutter`) with this directory’s contents at root. Push the mirror, set `repository: https://github.com/corpus-core/colibri-flutter` in [pubspec.yaml](pubspec.yaml), then publish.
   - **Option B:** Run `./scripts/prepare_pub_mirror.sh` from this directory to copy the package into a sibling folder; push that folder as the mirror repo, then set `repository` in pubspec to the mirror URL and publish.
3. From this directory: `dart pub publish --dry-run`, then `dart pub publish`.

## iOS note

iOS does not allow dynamic `dlopen` of external libraries. The XCFramework is linked into the app; the Dart FFI loader uses `DynamicLibrary.process()`.
