## 0.1.7

- Requires `colibri_stateless: ^0.1.7`. Removes witness function, fixes Linux build.

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
