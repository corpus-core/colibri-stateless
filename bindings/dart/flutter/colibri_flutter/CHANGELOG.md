## 0.2.3

- macOS: build the bundled universal `libcolibri.dylib` with the plugin's
  supported deployment targets (macOS 11.0 for `arm64`, 10.15 for `x86_64`).
  Version 0.2.2 inherited macOS 26.0 from the build host SDK and could not run
  on older supported macOS versions.

## 0.2.2

- macOS: ship a **universal** `libcolibri.dylib` (`arm64` + `x86_64`). The bundled
  `blst` static library is now compiled for the requested
  `CMAKE_OSX_ARCHITECTURES` slice, so Intel (`x86_64`) macOS apps link and run
  Colibri correctly. Previously the x86_64 slice was missing, causing
  `symbol(s) not found for architecture x86_64` at link time and missing `c4_*`
  symbols on Intel Macs at runtime.

## 0.2.1

- iOS: keep `c4_rpc_set_proxy_urls` in `force_link.c` (Dart FFI requires it; Release device builds dead-stripped it while simulator often still worked).
- macOS: add `s.dependency 'FlutterMacOS'` to the podspec so `import FlutterMacOS` resolves under Xcode Explicit Modules / current Flutter tooling.

## 0.2.0

- **Colibri v2** package line (`0.2.x`). Requires `colibri_stateless: ^0.2.0`.
- **iOS (SPM):** Swift Package Manager support via `ios/colibri_flutter/Package.swift` with `.binaryTarget` for `c4_swift.xcframework` (path shared with CocoaPods under `ios/colibri_flutter/Frameworks/`).
- iOS: fix missing `c4_create_prover_ctx` (and other FFI symbols) when Flutter builds the plugin as a dynamic framework (`use_frameworks!`).
  - `force_link.c` now keeps file-scope `__attribute__((used))` symbol pointers so Clang cannot empty the force-link object under optimization.
  - Podspec adds `-force_load` for the static `c4_swift` archive so Colibri objects are always linked into `colibri_flutter.framework`.

## 0.1.8

- Requires `colibri_stateless: ^0.1.8`.
- iOS: fixed XCFramework build – arm64 simulator slice now correctly compiled for simulator (not device), resolving `xcodebuild -create-xcframework` "binaries with multiple platforms" error.
- iOS: added `force_link.c` to retain all C symbols through linking (prevents `dlsym` lookup failures).
- iOS: added `s.dependency 'Flutter'` to podspec for Xcode 26+ Swift Explicit Module compatibility.
- Android: switched to static C++ STL linking (`c++_static`), fixing `libc++_shared.so` not found at runtime.
- Updated example app: `eth_getLogs` now queries blocks N-10..N-5 (prover requires block N+1 for `parentBeaconBlockRoot`).

## 0.1.7

- Requires `colibri_stateless: ^0.1.7`. Removes witness function, fixes Linux build.
- Native binaries (`.so`, `.xcframework`) are no longer checked into git; they are built on demand before publishing via `scripts/build_native_libs.sh`.
- Added `scripts/publish_colibri_flutter.sh` for automated build + publish workflow.
- Updated README with new build/publish instructions.

## 0.1.6

- Requires `colibri_stateless: ^0.1.6`. Aligns with updated FFI bindings (flags, PrivacyMode, optional witness support).

## 0.1.5

- Requires `colibri_stateless: ^0.1.5`. Aligns with Dart package 0.1.5 (ZK build flags, native link fix). Example app version 0.1.5+1.

## 0.1.4

- **Mobile default storage:** Requires `colibri_stateless: ^0.1.4`. On Android and iOS, Colibri now uses [MemoryStorage] by default when no storage is provided, so apps no longer need to pass `storage: MemoryStorage()` to avoid "invalid zk_proof!" or crashes.

## 0.1.3

- Fix Android stack overflow: Flutter example uses `MemoryStorage` to avoid non-writable `FILE_STORAGE` on mobile.
- Added macOS and Linux support with bundled native binaries.
- Build script: `--macos` and `--linux` (run on macOS/Linux host respectively).
- On desktop, use `Colibri(libraryPath: colibriFlutterLibraryPath)` so the bundled library is used.
- Extended example app with combo-button for Block Number, Block, and Logs tests.

## 0.1.2

- Version aligned with repository root `VERSION` (0.1.2). Depends on `colibri_stateless: ^0.1.2`.

## 0.1.1

- Version aligned with repository root `VERSION` (0.1.1). Depends on `colibri_stateless: ^0.1.1`.

## 0.1.0

- Initial release.
- Flutter plugin with bundled native Colibri binaries (Android: arm64-v8a, armeabi-v7a, x86_64; iOS: device + simulator).
- Re-exports `colibri_stateless`; no `libraryPath` needed on mobile.
