//! Bridge between the Rust [`Storage`](super::Storage) trait and the C
//! `storage_plugin_t` callbacks.
//!
//! There is a single global registration -- the C core only supports one
//! storage plugin at a time, so we install our trampolines on the first
//! call and swap out the backing Rust object atomically thereafter.

use super::Storage;
use crate::ffi::{buffer_grow, buffer_t, bytes_t, c4_set_storage_config, storage_plugin_t};
use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::{Arc, OnceLock, RwLock};

type StorageArc = Arc<dyn Storage>;

/// Holds the currently registered storage. Wrapped in an `RwLock` so
/// we can swap it out later without invalidating the callbacks.
static GLOBAL_STORAGE: OnceLock<RwLock<StorageArc>> = OnceLock::new();
/// Set to `true` after `c4_set_storage_config` has been called once.
static CALLBACKS_INSTALLED: OnceLock<()> = OnceLock::new();

fn current_storage() -> Option<StorageArc> {
    GLOBAL_STORAGE
        .get()
        .and_then(|lock| lock.read().ok().map(|g| Arc::clone(&*g)))
}

/// Called by the C core when it needs to read a key.
///
/// # Safety
///
/// - `key` must point to a NUL-terminated C string.
/// - `buffer` must point to a valid `buffer_t` owned by the caller.
///
/// All work happens inside `catch_unwind`; a panic in a user `Storage`
/// implementation is turned into a "miss" (returns `false`) instead of
/// unwinding through the C caller (which would be undefined behaviour).
unsafe extern "C" fn storage_get_callback(key: *mut c_char, buffer: *mut buffer_t) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if key.is_null() || buffer.is_null() {
            return false;
        }

        let key_str = match CStr::from_ptr(key).to_str() {
            Ok(s) => s,
            Err(_) => return false,
        };

        let storage = match current_storage() {
            Some(s) => s,
            None => return false,
        };

        let Some(data) = storage.get(key_str) else {
            return false;
        };

        // Reject blobs whose length would overflow `bytes_t::len`
        // (`u32`). Silently truncating would leave uninitialised bytes
        // in the destination buffer and mask a real bug.
        let required = data.len();
        if u32::try_from(required).is_err() {
            return false;
        }

        // Ask the C helper to grow the destination buffer -- it knows
        // how to respect the `allocated` sign convention (positive =
        // heap, negative = fixed/stack).
        let avail = buffer_grow(buffer, required);
        if avail < required {
            return false;
        }
        if (*buffer).data.data.is_null() {
            return false;
        }

        ptr::copy_nonoverlapping(data.as_ptr(), (*buffer).data.data, required);
        (*buffer).data.len = required as u32;
        true
    }))
    .unwrap_or(false)
}

/// Called by the C core when it needs to persist a key/value pair.
///
/// # Safety
///
/// - `key` must point to a NUL-terminated C string.
/// - `value.data` must point to at least `value.len` bytes.
///
/// Wrapped in `catch_unwind` -- see [`storage_get_callback`].
unsafe extern "C" fn storage_set_callback(key: *mut c_char, value: bytes_t) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if key.is_null() {
            return;
        }
        let key_str = match CStr::from_ptr(key).to_str() {
            Ok(s) => s,
            Err(_) => return,
        };
        let slice = if value.data.is_null() || value.len == 0 {
            &[][..]
        } else {
            std::slice::from_raw_parts(value.data, value.len as usize)
        };
        if let Some(storage) = current_storage() {
            storage.set(key_str, slice);
        }
    }));
}

/// Called by the C core when it wants to remove a key.
///
/// # Safety
///
/// `key` must point to a NUL-terminated C string.
///
/// Wrapped in `catch_unwind` -- see [`storage_get_callback`].
unsafe extern "C" fn storage_del_callback(key: *mut c_char) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if key.is_null() {
            return;
        }
        let key_str = match CStr::from_ptr(key).to_str() {
            Ok(s) => s,
            Err(_) => return,
        };
        if let Some(storage) = current_storage() {
            storage.delete(key_str);
        }
    }));
}

/// Install `storage` as the currently active backend, registering the
/// C callbacks on first use.
///
/// Only one storage plugin can be live at a time -- the C core keeps a
/// single global slot. Registering a new storage overwrites the
/// previous one; concurrent registrations are serialised through the
/// backing `RwLock` and the last writer wins deterministically.
pub(crate) fn register_global_storage(storage: Box<dyn Storage>) {
    let arc: StorageArc = Arc::from(storage);

    // `get_or_init` racelessly installs the RwLock on the first call
    // from any thread, and then every caller lands in the same lock.
    let lock = GLOBAL_STORAGE.get_or_init(|| RwLock::new(arc.clone()));
    if let Ok(mut guard) = lock.write() {
        *guard = arc;
    }

    // Register the callbacks with the C core exactly once.
    CALLBACKS_INSTALLED.get_or_init(|| {
        let mut plugin = storage_plugin_t {
            get: Some(storage_get_callback),
            set: Some(storage_set_callback),
            del: Some(storage_del_callback),
            max_sync_states: 3,
        };
        unsafe { c4_set_storage_config(&mut plugin as *mut _) };
    });
}

/// `true` when the callbacks have been installed on the C side.
#[allow(dead_code)] // useful for future diagnostics/tests
pub(crate) fn is_installed() -> bool {
    CALLBACKS_INSTALLED.get().is_some()
}
