## 0.1.9

- Web: conditional export uses `dart.library.ffi` so the web compiler does not load the FFI package; default export is `client_web.dart` for web.
- Dart/Flutter web: Colibri loads from CDN via plugin bootstrap script; no manual WASM build required for web apps.

## 0.1.8

- Configurable timeouts: added `rpcTimeout` (default 30s) and `proverTimeout` (default 120s) to `Colibri` constructor, replacing hardcoded 30s timeouts.
- iOS: fixed `DynamicLibrary` loading – uses `executable()` with `process()` fallback for static XCFramework symbol resolution.
- iOS: fixed XCFramework build for arm64 simulator (CMake now correctly targets `arm64-apple-ios-simulator` instead of device).
- iOS: added `force_link.c` to prevent linker dead-stripping of C symbols used via `dlsym`.
- iOS: added `s.dependency 'Flutter'` to podspec for Xcode 26+ compatibility.
- Android: fixed runtime crash by statically linking C++ STL (`c++_static` instead of `c++_shared`).

## 0.1.7

- Removed `c4_verify_create_ctx_with_witness` from C bindings and Dart FFI layer.
- Fixed Linux shared library build: added `CMAKE_POSITION_INDEPENDENT_CODE` for DART target.
- Removed root-level `pubspec.yaml`, `pubspec.lock`, and `lib/colibri_stateless.dart` (monorepo cleanup; canonical package is in `bindings/dart/`).
- Updated publishing docs and README to reflect current build/publish workflow.

## 0.1.6

- Updated FFI bindings to match current C API: added `flags` parameter to `c4_verify_create_ctx` and `c4_get_method_support`, added `PrivacyMode` enum and `useAccesslist` support.
- Made `c4_verify_create_ctx_with_witness` lookup optional for builds that don't export the symbol.
- Integration tests now read `include_code` and `use_accesslist` from test fixtures.

## 0.1.5

- Flutter build script: explicit `-DETH_ZKPROOF=ON` for Android, macOS, Linux, Windows so ZK proof support is always included.
- Native Dart library: link `blst` and `crypto` into the shared library so macOS (and other platforms) resolve BLS/precompile symbols.

## 0.1.4

- **Mobile default storage:** On Android and iOS, if no [storage] is passed to [Colibri], [MemoryStorage] is now used automatically so the native cache works without app code changes. Fixes "invalid zk_proof!" and crashes when integrating colibri_flutter in other apps.

## 0.1.3

- Fix Android stack overflow caused by `FILE_STORAGE` writing to non-writable CWD on mobile; Flutter example now uses `MemoryStorage`.
- Fix `req_ptr` JSON precision loss for 64-bit pointers (emit as string).
- Added `onDebug` diagnostic callbacks for prover responses and proof verification.
- Extended Flutter example with combo-button for Block Number, Block, and Logs tests.

## 0.1.2

- Package version aligned with repository root `VERSION` (0.1.2). Use `scripts/sync_version.sh` to update from root before publishing.

## 0.1.1

- Package version aligned with repository root `VERSION` (0.1.1). Use `scripts/sync_version.sh` to update from root before publishing.

## 0.1.0

- Initial release.
- Dart FFI bindings for Colibri Stateless (proof generation and verification).
- Support for proofable, unproofable, and local RPC methods.
- Optional storage plugin and configurable prover/beacon/RPC endpoints.
