## 0.1.9

- iOS: fixed Release/Profile builds where all `c4_*` symbols were stripped by the optimizing compiler. Replaced local `volatile` array in `force_link.c` with a global exported symbol array that survives `-Os`/`-O2`. Added `-force_load` linker flag in podspec as additional safeguard.
- Aligned podspec versions (iOS/macOS) with pubspec version.

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
