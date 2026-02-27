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
