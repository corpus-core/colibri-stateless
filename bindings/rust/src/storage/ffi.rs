use std::ffi::{CStr, c_char};
use std::sync::{Arc, Mutex, OnceLock};
use std::ptr;
use super::Storage;
use crate::ffi::{buffer_t, bytes_t, storage_plugin_t, c4_set_storage_config, buffer_grow};

// Global storage instance
static GLOBAL_STORAGE: OnceLock<Arc<Mutex<Box<dyn Storage>>>> = OnceLock::new();
// Track if registered with C library

static mut STORAGE_REGISTERED_WITH_C: bool = false;

unsafe extern "C" fn storage_get_callback(key: *mut c_char, buffer: *mut buffer_t) -> bool {
    if key.is_null() || buffer.is_null() {
        return false;
    }

    let key_str = match CStr::from_ptr(key).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    if let Some(storage) = GLOBAL_STORAGE.get() {
        if let Ok(guard) = storage.lock() {
            if let Some(data) = guard.get(key_str) {
                let required_len = data.len() + 1;
                buffer_grow(buffer, required_len);

                if !(*buffer).data.data.is_null() {
                    ptr::copy_nonoverlapping(data.as_ptr(), (*buffer).data.data, data.len());
                    (*buffer).data.len = data.len() as u32;
                    return true;
                }
            }
        }
    }

    false
}

unsafe extern "C" fn storage_set_callback(key: *mut c_char, value: bytes_t) {
    if key.is_null() || value.data.is_null() || value.len == 0 {
        return;
    }

    let key_str = match CStr::from_ptr(key).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };

    let data_slice = std::slice::from_raw_parts(value.data, value.len as usize);

    if let Some(storage) = GLOBAL_STORAGE.get() {
        if let Ok(guard) = storage.lock() {
            guard.set(key_str, data_slice);
        }
    }
}

unsafe extern "C" fn storage_delete_callback(key: *mut c_char) {
    if key.is_null() {
        return;
    }

    let key_str = match CStr::from_ptr(key).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };

    if let Some(storage) = GLOBAL_STORAGE.get() {
        if let Ok(guard) = storage.lock() {
            guard.delete(key_str);
        }
    }
}

/// Register storage with the C library. Idempotent.
pub(crate) fn register_global_storage(storage: Box<dyn Storage>) {
    let _ = GLOBAL_STORAGE.set(Arc::new(Mutex::new(storage)));

    unsafe {
        if !STORAGE_REGISTERED_WITH_C {
            let mut plugin = storage_plugin_t {
                get: Some(storage_get_callback),
                set: Some(storage_set_callback),
                del: Some(storage_delete_callback),
                max_sync_states: 3,
            };
            c4_set_storage_config(&mut plugin);
            STORAGE_REGISTERED_WITH_C = true;
        }
    }
}

#[allow(dead_code)]
pub(crate) fn is_storage_registered() -> bool {
    unsafe { STORAGE_REGISTERED_WITH_C }
}

#[allow(dead_code)]
pub(crate) fn cleanup_global_storage() {
    unsafe {
        if STORAGE_REGISTERED_WITH_C {
            let mut plugin = storage_plugin_t {
                get: None,
                set: None,
                del: None,
                max_sync_states: 0,
            };
            c4_set_storage_config(&mut plugin);
            STORAGE_REGISTERED_WITH_C = false;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::super::MemoryStorage;
    use crate::ffi::c4_get_storage_config;

    #[test]
    fn test_storage_registration() {
        let storage = MemoryStorage::new();
        register_global_storage(storage);
        assert!(is_storage_registered());
    }

    #[test]
    fn test_storage_callbacks_with_global() {
        let storage = MemoryStorage::new();
        register_global_storage(storage);

        if let Some(s) = GLOBAL_STORAGE.get() {
            let guard = s.lock().unwrap();
            guard.set("test_key", b"test_value");
            assert_eq!(guard.get("test_key"), Some(b"test_value".to_vec()));
            guard.delete("test_key");
            assert_eq!(guard.get("test_key"), None);
        }
    }

    #[test]
    fn test_c_library_sees_our_callbacks() {
        let storage = MemoryStorage::new();
        register_global_storage(storage);

        unsafe {
            let mut plugin = storage_plugin_t {
                get: None,
                set: None,
                del: None,
                max_sync_states: 0,
            };
            c4_get_storage_config(&mut plugin);

            assert!(plugin.get.is_some());
            assert!(plugin.set.is_some());
            assert!(plugin.del.is_some());
            assert!(plugin.max_sync_states > 0);

            assert_eq!(plugin.get.unwrap() as usize, storage_get_callback as usize);
            assert_eq!(plugin.set.unwrap() as usize, storage_set_callback as usize);
            assert_eq!(plugin.del.unwrap() as usize, storage_delete_callback as usize);
        }
    }

    #[test]
    fn test_c_can_call_our_storage_callbacks() {
        use std::ffi::CString;

        let storage = MemoryStorage::new();
        register_global_storage(storage);

        if let Some(s) = GLOBAL_STORAGE.get() {
            s.lock().unwrap().set("c_test_key", b"hello_from_rust");
        }

        unsafe {
            let mut plugin = storage_plugin_t {
                get: None,
                set: None,
                del: None,
                max_sync_states: 0,
            };
            c4_get_storage_config(&mut plugin);

            let mut buffer = buffer_t {
                data: bytes_t { len: 0, data: std::ptr::null_mut() },
                allocated: 0,
            };

            let key = CString::new("c_test_key").unwrap();
            let found = (plugin.get.unwrap())(key.as_ptr() as *mut _, &mut buffer);

            assert!(found);
            assert_eq!(buffer.data.len, 15);
        }
    }
}