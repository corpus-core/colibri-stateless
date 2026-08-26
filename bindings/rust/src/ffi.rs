//! Raw FFI declarations for the Colibri C API.
//!
//! This file mirrors `bindings/colibri.h` and `src/util/plugin.h`. The
//! declarations are hand-written (and structurally match what `rust-bindgen`
//! would produce for the same headers) so the crate has no build-time
//! dependency on `bindgen`/libclang and can be published on crates.io
//! without pulling llvm.
//!
//! All declarations are `unsafe`. Safe wrappers live in
//! [`crate::core`] and [`crate::storage`].

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::{c_char, c_int, c_void};

/// Opaque handle for the prover context.
pub type prover_t = c_void;

/// Fat pointer wrapping a byte span. Mirrors `bytes_t` from
/// `src/util/bytes.h` -- `len` is a plain `uint32_t`, so the pointer is
/// unaligned relative to `len`. The C layout is `{ uint32_t len; uint8_t* data; }`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct bytes_t {
    pub len: u32,
    pub data: *mut u8,
}

/// Growable byte buffer. Mirrors `buffer_t` in `src/util/bytes.h`.
///
/// `allocated > 0` means the buffer owns heap memory; `allocated < 0`
/// signals a fixed / stack buffer that must not be freed. Rust code only
/// uses this type when interacting with the storage plugin (grow-only
/// path).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct buffer_t {
    pub data: bytes_t,
    pub allocated: i32,
}

/// Storage plugin callback: read a key. Returns `true` when the key was
/// found and the data was written into `buffer` (grown as needed).
pub type storage_get_fn =
    Option<unsafe extern "C" fn(key: *mut c_char, buffer: *mut buffer_t) -> bool>;

/// Storage plugin callback: write a key/value pair.
pub type storage_set_fn = Option<unsafe extern "C" fn(key: *mut c_char, value: bytes_t)>;

/// Storage plugin callback: delete a key.
pub type storage_del_fn = Option<unsafe extern "C" fn(key: *mut c_char)>;

/// Storage plugin configuration passed to [`c4_set_storage_config`].
/// Mirrors `storage_plugin_t` in `src/util/plugin.h`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct storage_plugin_t {
    pub get: storage_get_fn,
    pub set: storage_set_fn,
    pub del: storage_del_fn,
    /// Maximum number of stored sync-committee state slots (rotation window).
    pub max_sync_states: u32,
}

extern "C" {
    // ------------------------------------------------------------------
    // Prover API (bindings/colibri.h)
    // ------------------------------------------------------------------

    pub fn c4_create_prover_ctx(
        method: *mut c_char,
        params: *mut c_char,
        chain_id: u64,
        flags: u32,
    ) -> *mut prover_t;

    pub fn c4_prover_execute_json_status(ctx: *mut prover_t) -> *mut c_char;

    pub fn c4_prover_get_proof(ctx: *mut prover_t) -> bytes_t;

    pub fn c4_free_prover_ctx(ctx: *mut prover_t);

    // ------------------------------------------------------------------
    // Verifier API
    // ------------------------------------------------------------------

    pub fn c4_verify_create_ctx(
        proof: bytes_t,
        method: *mut c_char,
        args: *mut c_char,
        chain_id: u64,
        trusted_checkpoint: *mut c_char,
        flags: u32,
    ) -> *mut c_void;

    pub fn c4_verify_execute_json_status(ctx: *mut c_void) -> *mut c_char;

    pub fn c4_verify_set_min_latest_block_ts(ctx: *mut c_void, ts: u64);

    pub fn c4_verify_free_ctx(ctx: *mut c_void);

    // ------------------------------------------------------------------
    // Request response handling (host <-> core)
    // ------------------------------------------------------------------

    pub fn c4_req_set_response(req_ptr: *mut c_void, data: bytes_t, node_index: u16);

    pub fn c4_req_set_error(req_ptr: *mut c_void, error: *mut c_char, node_index: u16);

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------

    /// Query whether an RPC method is proofable / unproofable / local /
    /// unsupported. Returns a `c4_method_type_t` value (see `MethodType`).
    ///
    /// `params` is a JSON string of the method params (may be NULL for
    /// static checks); `flags` is a set of `verify_flags_t` bits (e.g.
    /// PAP mode may promote a method to LOCAL when the storage cache is
    /// hot).
    pub fn c4_get_method_support(
        chain_id: u64,
        method: *mut c_char,
        params: *mut c_char,
        flags: u32,
    ) -> c_int;

    pub fn c4_get_current_version_number() -> u32;

    // ------------------------------------------------------------------
    // Unified RPC API
    // ------------------------------------------------------------------

    /// `prover_mode`: 0 LOCAL, 1 REMOTE, 2 HYBRID, 3 PROXY.
    pub fn c4_create_rpc_ctx(
        method: *mut c_char,
        params: *mut c_char,
        chain_id: u64,
        prover_flags: u32,
        verify_flags: u32,
        prover_mode: c_int,
    ) -> *mut c_void;

    pub fn c4_rpc_execute_json_status(ctx: *mut c_void) -> *mut c_char;

    pub fn c4_free_rpc_ctx(ctx: *mut c_void);

    pub fn c4_rpc_set_witness_keys(ctx: *mut c_void, witness_keys: *const c_char);

    pub fn c4_rpc_set_proxy_urls(
        ctx: *mut c_void,
        rpc_urls: *const c_char,
        beacon_urls: *const c_char,
    );

    pub fn c4_rpc_set_min_latest_block_ts(ctx: *mut c_void, ts: u64);

    /// Configure a chain-specific trusted checkpoint (hex string, may be
    /// NULL to keep the default). Global, not per-context.
    pub fn c4_set_checkpoint(chain_id: u64, trusted_checkpoint: *const c_char);

    /// Clear in-process prover/verifier caches (test isolation).
    pub fn c4_reset_caches();

    // ------------------------------------------------------------------
    // Storage plugin (src/util/plugin.h)
    // ------------------------------------------------------------------

    pub fn c4_set_storage_config(plugin: *mut storage_plugin_t);

    #[allow(dead_code)] // exposed for tests + future use
    pub fn c4_get_storage_config(plugin: *mut storage_plugin_t);

    // ------------------------------------------------------------------
    // Buffer helper (src/util/bytes.h) -- used by the storage `get`
    // callback to grow `buffer_t.data` before we memcpy into it.
    // ------------------------------------------------------------------

    pub fn buffer_grow(buffer: *mut buffer_t, min_len: usize) -> usize;
}
