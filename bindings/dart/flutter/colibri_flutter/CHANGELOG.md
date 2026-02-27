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
