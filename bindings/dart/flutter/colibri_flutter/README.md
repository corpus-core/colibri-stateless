# Colibri Flutter

Flutter plugin for **Colibri v2** (package line `0.2.x`) with **bundled native binaries** (Android, iOS, macOS, Linux). No manual build or `libraryPath` needed on mobile; on desktop use `colibriFlutterLibraryPath`.

Depends on [`colibri_stateless`](https://pub.dev/packages/colibri_stateless) `^0.2.0`.

## Install (pub.dev)

```yaml
dependencies:
  colibri_flutter: ^0.2.1
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

On Android and iOS the native library is loaded automatically; `colibriFlutterLibraryPath` is `null` there. On macOS and Linux, pass `colibriFlutterLibraryPath` so the plugin's bundled library is used.

**Flutter web is not supported** (no native FFI). Use Android, iOS, macOS, Linux, or Windows.

If you use `assets/.env` for configuration: do not commit real secrets; use other configuration (e.g. environment or secure storage) for production.

## Testing the plugin

**1. Run the example app** (from this repo):

```bash
cd bindings/dart/flutter/colibri_flutter/example
flutter pub get
flutter run
# Pick a device (e.g. Android emulator or iOS simulator), then tap "Fetch block number".
```

Prefer testing **Release on a physical iPhone** as well as the simulator — some FFI link issues only show up on device Release builds.

**2. In your own app**

- Add dependency: `colibri_flutter: ^0.2.1` (and run `flutter pub get`).
- Use the same constructor so the plugin's library is used on all platforms:

```dart
final colibri = Colibri(chainId: 1, libraryPath: colibriFlutterLibraryPath);
```

- **Android**: The plugin loads `libcolibri.so` from `jniLibs` when the engine attaches. No path needed; `colibriFlutterLibraryPath` is `null`.
- **iOS**: The XCFramework is linked into the app; no path needed. CocoaPods and Swift Package Manager both use `ios/colibri_flutter/`.
- **macOS / Linux**: You must pass `colibriFlutterLibraryPath` and have built the desktop binaries (see "Building native binaries" below).

**3. If the app crashes or FFI fails on the first Colibri call**

- **Android**: Ensure the plugin's `jniLibs` contain `libcolibri.so` for your ABI (arm64-v8a, armeabi-v7a, x86_64). If you depend on the published package, they are included. If you use a path dependency, run `./scripts/build_native_libs.sh --android` from the plugin directory or `./scripts/build_flutter_binaries.sh --android` from the repo root.
- **iOS – missing symbol** (e.g. `c4_create_prover_ctx`, `c4_rpc_set_proxy_urls`): Dart FFI uses `DynamicLibrary.process()`. With Flutter `use_frameworks!`, Colibri is a static archive linked into `colibri_flutter.framework`. Symbols used only via `dlsym` must be retained by `ios/colibri_flutter/Sources/colibri_force_link/force_link.c` and `-force_load` in the podspec. Simulator/Debug can appear fine while **Release on device** fails if a keeper is missing. Ensure you are on **≥ 0.2.1**, clean rebuild (`flutter clean`, `pod install`), and that every symbol looked up in `colibri_stateless` `native.dart` has a `COLIBRI_KEEP(...)` entry.
- **iOS – XCFramework missing** (path dependency): Build with `./scripts/build_native_libs.sh --ios` so `ios/colibri_flutter/Frameworks/c4_swift.xcframework` exists.
- **macOS – `unable to resolve module dependency: 'FlutterMacOS'`**: The macOS podspec must declare `s.dependency 'FlutterMacOS'` (included since **0.2.1**). Run `flutter clean` and rebuild.
- **Desktop**: Pass `libraryPath: colibriFlutterLibraryPath` and ensure you have run the build script for macOS/Linux so the bundled lib exists.

## Local / path dependency

For development against a local checkout:

```yaml
dependencies:
  colibri_flutter:
    path: /path/to/colibri-stateless/bindings/dart/flutter/colibri_flutter
  colibri_stateless:
    path: /path/to/colibri-stateless/bindings/dart
```

Build native binaries in the plugin directory before running (see below).

## Building native binaries (for maintainers)

Native binaries (`.so`, `.xcframework`) are **not** checked into git. They must be built before publishing or when using a path dependency from a local checkout.

**From this directory** (recommended):

```bash
./scripts/build_native_libs.sh --all
# Or per platform:
./scripts/build_native_libs.sh --android   # requires ANDROID_NDK_HOME
./scripts/build_native_libs.sh --ios       # macOS + Xcode only
```

**Or from the repo root** (also builds macOS/Linux/Windows desktop binaries):

```bash
./scripts/build_flutter_binaries.sh
# Or per platform:
./scripts/build_flutter_binaries.sh --android
./scripts/build_flutter_binaries.sh --ios
./scripts/build_flutter_binaries.sh --macos   # macOS only (universal arm64 + x86_64)
./scripts/build_flutter_binaries.sh --linux   # Linux host only
./scripts/build_flutter_binaries.sh --windows
```

Output locations:

- `android/src/main/jniLibs/<abi>/libcolibri.so`
- `ios/colibri_flutter/Frameworks/c4_swift.xcframework` (CocoaPods + Swift Package Manager)
- `macos/Frameworks/libcolibri.dylib` (universal; repo-root script only)
- `linux/lib/libcolibri.so` (repo-root script only)

## iOS integration notes

- iOS does not allow dynamic `dlopen` of external libraries. The XCFramework is linked into the app; Dart FFI uses `DynamicLibrary.process()` / `executable()`.
- Layout: `ios/colibri_flutter/` holds `Package.swift`, `Sources/` (Swift plugin + `colibri_force_link`), and `Frameworks/c4_swift.xcframework`. The CocoaPods podspec points at the same tree.
- When adding new C API symbols used from Dart, update **both** `colibri_stateless` FFI bindings and `force_link.c` (`COLIBRI_KEEP`), then bump the plugin patch version.

## Publishing (pub.dev)

1. Publish **colibri_stateless** (`0.2.x`) first from `bindings/dart`.
2. Build binaries and publish in one step:

```bash
./scripts/publish_colibri_flutter.sh --dry-run   # build + check
./scripts/publish_colibri_flutter.sh             # build + publish
```

The publish script calls `build_native_libs.sh --all`, verifies the binaries exist, then publishes from an isolated temp copy (so monorepo `.gitignore` / parent `.pubignore` do not hide `pubspec.yaml` or the native binaries).

3. **Repository verification:** pub.dev expects the [repository](pubspec.yaml) URL to clone to a repo that contains a `pubspec.yaml` with `name: colibri_flutter` at root. This package lives in a monorepo subdirectory, so to pass that check either:
   - **Option A:** Create a mirror repo (e.g. `corpus-core/colibri-flutter`) with this directory's contents at root. Push the mirror, set `repository: https://github.com/corpus-core/colibri-flutter` in [pubspec.yaml](pubspec.yaml), then publish.
   - **Option B:** Run `./scripts/prepare_pub_mirror.sh` from this directory to copy the package into a sibling folder; push that folder as the mirror repo, then set `repository` in pubspec to the mirror URL and publish.
4. Manual alternative: `dart pub publish --dry-run`, then `dart pub publish` (after building binaries separately).
