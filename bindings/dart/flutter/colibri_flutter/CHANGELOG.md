## 0.1.16

- **ZK verification (all platforms):** Build the bundled native binaries with a v6-capable client version (`C4_VERSION >= 2.0.0`). ZK sync verification uses SP1 v6, and the prover only serves v6 sync proofs to clients reporting version `>= 2.0.0`; binaries built from the current 1.x git tag requested the legacy v5 proof, which the v6-only verifier rejected with `VK not found for program hash` -> `invalid zk_proof!` (seen on macOS with `zkProof: true`). Build scripts now force a v6-capable version unless one is provided explicitly.

## 0.1.15

- **macOS (SPM):** Move native binaries to `macos/colibri_flutter/Frameworks/` and reference `Frameworks/libcolibri.xcframework` from `Package.swift` (fixes Flutter SPM resolving `../Frameworks/…` to a non-existent ephemeral path in 0.1.14).

## 0.1.14

- **macOS (SPM):** Bundle `libcolibri.xcframework` via Swift Package Manager `.binaryTarget` so Xcode embeds `libcolibri.dylib` into `<App>.app/Contents/Frameworks/` when using Flutter's Swift Package Manager integration (fixes `Failed to load dynamic library` on first FFI call).
- **macOS (SPM):** `Package.swift` declares `ColibriNative` binary target and FlutterFramework dependency per Flutter 3.44+ plugin SPM template.
- **macOS:** Build scripts now produce both `macos/colibri_flutter/Frameworks/libcolibri.dylib` (CocoaPods) and `macos/colibri_flutter/Frameworks/libcolibri.xcframework` (SPM). Publish builds use universal slices when `BUILD_MACOS_UNIVERSAL=1`.
- **macOS:** `colibriFlutterLibraryPath` probes `libcolibri.dylib` and `libcolibri.framework/libcolibri` under `Contents/Frameworks/`.

## 0.1.13

- **macOS:** Swift Package Manager support (`macos/colibri_flutter/Package.swift`); silences Flutter 3.44+ SPM migration warnings for this plugin.

## 0.1.12

- **macOS:** Rebuilt `libcolibri.dylib` against current C core (fixes missing `c4_rpc_set_min_latest_block_ts` / proof-freshness APIs in 0.1.11).
- **Build:** `build_native_libs.sh` now builds macOS (and Linux on Linux hosts); publish script verifies macOS binary freshness.
- **Build:** `build_flutter_binaries.sh` builds host architecture by default on macOS (`BUILD_MACOS_UNIVERSAL=1` for arm64+x86_64).
- Requires `colibri_stateless: ^0.1.12`.

## 0.1.11

- Migrates Android build to Flutter built-in Kotlin (removes `kotlin-android` / KGP from the plugin).
- Requires Flutter `>=3.44.0` and Dart `>=3.12.0`.

## 0.1.10

- Requires `colibri_stateless: ^0.1.10`.
- iOS: `force_link.c` updated for `c4_rpc_set_min_latest_block_ts`.
- Native binaries rebuilt against current C core (freshness, oblivious RPC, EVM revert handling).

## 0.1.9

- Published on pub.dev; use 0.1.10 for the current release.

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
