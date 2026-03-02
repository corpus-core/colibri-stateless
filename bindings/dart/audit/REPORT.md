# Colibri Dart Bindings Audit

Date: 2026-02-19 (last updated: 2026-02-19)

## Scope
- Dart sources:
  - `bindings/dart/lib/src/client.dart`
  - `bindings/dart/lib/src/native.dart`
  - `bindings/dart/lib/src/types.dart`
  - `bindings/dart/lib/src/storage.dart`
- Test utilities:
  - `bindings/dart/test/test_helpers.dart`
- READMEs, test coverage, and debug-logging behaviour (sensitive data).

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

### Addressed: Sensitive data in debug logging
Previously, when `logProverRequests` was true, `print()` could output witness/signer
keys in plain text; the `onDebug` callback could receive full `witnessKeys` values.
If enabled in production or forwarded to production logging, this could leak
sensitive data.

Mitigation (client.dart):
- `logProverRequests`: only non-sensitive summaries are printed (e.g. `signers_length`
  instead of raw signers). Dart-doc states debug-only and not for production.
- `onDebug`: messages now redact witness keys (e.g. `witnessKeys=***` when set).
  Dart-doc warns that callbacks can contain sensitive data and must not be forwarded
  to production logging without redaction.

Recommendation for users: Do not enable `logProverRequests` in production; do not
forward `onDebug` to production logs without redaction.

## Positive Checks
- FFI ownership: proof and response data are copied where required; no use-after-free
  observed in Dart bindings.
- Returned JSON status strings are freed via libc `free`, matching C allocation.
- Pending request handling uses parallel futures and respects exclude masks.
- Method support mapping aligns with C API return values.
- No hardcoded secrets or API keys in repo; `.env` not committed (only `.env.example`);
  production default URLs use HTTPS.
- README version and `run_tests.sh` documented; DataRequest and Colibri constructor
  (including onDebug/logProverRequests) fully documented.
- Tests added for verifyProof error handling (invalid proof → VerificationError),
  onDebug callback invocation, and close() idempotency.

The findings above (global storage, library path, TLS) are accepted trade-offs and
are documented for the auditor.

## Tests
- `dart test` (pass). Proof error test, onDebug test, and close-idempotency test
  have been added; integration and client tests cover the main code paths.

## Notes
- Windows: libc `free()` uses `msvcrt.dll`. If the native library is built with a
  different CRT, this could cause mismatched allocation/free. This is a standard
  FFI caveat; ensure consistent toolchain on Windows builds.

---

## Senior Auditor Review (Cross-Binding and C API Alignment)

**Review date:** 2026-02-19  
**Scope:** Security, code quality, alignment with C API and Python binding.

### C API and Ownership

- **Verify context:** C `c4_verify_create_ctx` / `c4_verify_create_ctx_with_witness` take `char*` for method, args, trusted_checkpoint, witness_keys and internally `strdup()` them. The Dart binding allocates with `toNativeUtf8()`, passes pointers, then frees its own copies with `malloc.free()` after the call. Correct; no double-free or use-after-free.
- **Prover context:** C `c4_create_prover_ctx` uses the passed pointers only during the call (caller can free after). Dart frees method/params after the call. Correct.
- **Status strings:** C returns newly allocated JSON strings; caller must free. Dart uses `_free(resultPtr.cast())` (libc `free`) for every `proverExecuteJsonStatus` / `verifyExecuteJsonStatus` result. Matches C allocation policy.
- **req_set_error:** C `c4_req_set_error` does `strdup(error)`. Dart frees its `errorPtr` after the call. Correct.
- **Storage get callback:** C `buffer_append()` copies the provided bytes into the buffer (`memcpy` in `src/util/bytes.c`). The Dart storage get callback allocates data, calls `_bufferAppend`, then frees the allocation. No use-after-free; C does not retain the pointer.
- **reqSetResponse:** Dart allocates `dataPtr` and a `BytesT` wrapper, passes to C; C `bytes_dup()` copies the data. Dart frees `dataPtr` and the wrapper after the call. Correct.

### Comparison with Python Binding

- **Witness keys (ZK):** The Python binding exposes only `c4_verify_create_ctx` (five arguments) and does not pass `checkpoint_witness_keys` to the verifier. The Dart binding correctly uses `c4_verify_create_ctx_with_witness` and passes `checkpointWitnessKeys`. Dart is feature-complete for ZK verification; Python is not yet.
- **Storage:** Both use a global storage plugin. Python has `clear_storage()`; Dart has `ColibriNative.clearStorage()`. Parity.
- **RPC flow:** Both implement prover-first then local fallback, verify-then-return, and the same fallback when verification returns null (prover JSON). Logic aligned with Python and C state machine.
- **DataRequest.req_ptr:** C emits `req_ptr` as string in JSON for 64-bit portability. Dart `DataRequest.fromJson` parses both int and string and normalises 64-bit values (including sign extension) via `parseReqPtr`. Python uses integer; Dart handling is more robust for 64-bit pointers in JSON.

### Security

- No additional issues beyond those already listed (global storage, library path, TLS). Sensitive data in logging is mitigated (redaction and documentation).
- No injection surface: RPC params are JSON-encoded and passed through; C parses and owns copies. No `eval` or dynamic code execution.
- Storage callbacks catch exceptions and return 0 or no-op; no uncaught exceptions into C.

### Code Quality

- **Error handling:** Verification and proof errors are mapped to `VerificationError` / `ProofError` with messages from C or safe defaults. RPC and HTTP errors are wrapped with `RPCError` / `HTTPError`.
- **Types:** `DataRequest` fields and constructor are documented; `req_ptr` parsing handles 64-bit and string/int from C consistently.
- **clearStorage:** Implemented and used to avoid stale handler use; aligns with Python’s `clear_storage` and supports multi-instance/testing hygiene.

### Conclusion

The Dart binding is correctly implemented against the C API with proper ownership and lifecycle handling. It is ahead of the Python binding for ZK (witness keys) and has equivalent or better handling of global storage and 64-bit `req_ptr`. No further security or correctness issues were identified for the scope of this review.

