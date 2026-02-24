# Colibri Dart Bindings Audit

Date: 2026-02-19

## Scope
- Dart sources:
  - `bindings/dart/lib/src/client.dart`
  - `bindings/dart/lib/src/native.dart`
  - `bindings/dart/lib/src/types.dart`
  - `bindings/dart/lib/src/storage.dart`
- Test utilities:
  - `bindings/dart/test/test_helpers.dart`

## Summary
The Dart bindings are functional and the core FFI interactions align with the C API
ownership rules (strings and bytes are copied where required, returned strings are
freed with libc). The high-level API correctly mirrors the C state machine for
proof creation and verification. Security risks are low and mostly relate to
expected trade-offs (dynamic library loading, network transport without pinning).

## Tests Executed
- `dart test` (pass)

## Findings

### Low: Global storage handler is process-wide
`ColibriNative.registerStorage()` installs a global storage handler used by native
callbacks. This mirrors the C plugin design, but it means multiple `Colibri`
instances in the same process share storage. This can lead to cross-test or
cross-instance contamination if different storage implementations are used.

Impact:
- In multi-instance usage, storage is not isolated per client.

Recommendation:
- Document that storage is global, or expose a `clearStorage()` call after use.

### Low: Dynamic library path is user-controlled
The binding allows overriding the native library path via `COLIBRI_DART_LIBRARY`
or constructor argument. This is standard for FFI, but loading arbitrary paths
can be a security risk if the input is untrusted.

Impact:
- If an attacker controls the path or environment, they could load a malicious
  shared library.

Recommendation:
- Keep this as-is, but document that the path must be trusted.

### Low: No TLS pinning / transport hardening
HTTP requests rely on system TLS defaults and do not implement pinning or
certificate validation beyond the platform defaults.

Impact:
- Standard risk for network clients; generally acceptable.

Recommendation:
- If higher assurance is needed, allow injecting a custom HTTP client.

## Positive Checks
- FFI ownership: proof and response data are copied where required; no use-after-free
  observed in Dart bindings.
- Returned JSON status strings are freed via libc `free`, matching C allocation.
- Pending request handling uses parallel futures and respects exclude masks.
- Method support mapping aligns with C API return values.

## Notes
- Windows: libc `free()` uses `msvcrt.dll`. If the native library is built with a
  different CRT, this could cause mismatched allocation/free. This is a standard
  FFI caveat; ensure consistent toolchain on Windows builds.

