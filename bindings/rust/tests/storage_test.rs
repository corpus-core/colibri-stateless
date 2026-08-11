//! Unit tests for the built-in [`Storage`] implementations.
//!
//! These do not touch the C core; they exercise the pure-Rust
//! backends that most downstream users end up using.

use std::env;

use colibri_stateless::storage::{FileStorage, MemoryStorage, Storage};

#[test]
fn memory_storage_round_trip() {
    let s = MemoryStorage::new();
    assert!(s.get("k").is_none());
    s.set("k", b"v");
    assert_eq!(s.get("k").as_deref(), Some(&b"v"[..]));

    // Overwrite the value.
    s.set("k", b"w");
    assert_eq!(s.get("k").as_deref(), Some(&b"w"[..]));

    // Delete removes the entry.
    s.delete("k");
    assert!(s.get("k").is_none());
}

#[test]
fn memory_storage_multiple_keys_independent() {
    let s = MemoryStorage::new();
    s.set("a", b"one");
    s.set("b", b"two");
    assert_eq!(s.get("a").as_deref(), Some(&b"one"[..]));
    assert_eq!(s.get("b").as_deref(), Some(&b"two"[..]));
    s.delete("a");
    assert!(s.get("a").is_none());
    assert_eq!(s.get("b").as_deref(), Some(&b"two"[..]));
}

#[test]
fn file_storage_round_trip() {
    let dir = tempfile::tempdir().expect("tempdir");
    let s = FileStorage::new(Some(dir.path().to_path_buf())).expect("file storage");

    assert!(s.get("k").is_none());
    s.set("k", b"payload");
    assert_eq!(s.get("k").as_deref(), Some(&b"payload"[..]));

    s.set("k", b"payload2");
    assert_eq!(s.get("k").as_deref(), Some(&b"payload2"[..]));

    s.delete("k");
    assert!(s.get("k").is_none());
}

#[test]
fn file_storage_sanitises_unsafe_keys() {
    let dir = tempfile::tempdir().expect("tempdir");
    let s = FileStorage::new(Some(dir.path().to_path_buf())).expect("file storage");

    // Simulate a key that carries filesystem-hostile characters --
    // set/get must remain consistent, and no file with the unsafe
    // characters may hit disk.
    let raw = "eth:call/latest?block=1";
    s.set(raw, b"payload");
    assert_eq!(s.get(raw).as_deref(), Some(&b"payload"[..]));

    let entries: Vec<_> = std::fs::read_dir(dir.path())
        .expect("read dir")
        .filter_map(|e| e.ok().map(|e| e.file_name().to_string_lossy().into_owned()))
        .collect();
    assert_eq!(entries.len(), 1, "expected exactly one file: {entries:?}");
    let name = &entries[0];
    for bad in [':', '/', '?', '='] {
        assert!(
            !name.contains(bad),
            "sanitiser leaked '{bad}' into filename: {name}"
        );
    }
}

#[test]
fn file_storage_survives_path_traversal_attempt() {
    let dir = tempfile::tempdir().expect("tempdir");
    let s = FileStorage::new(Some(dir.path().to_path_buf())).expect("file storage");

    s.set("../evil", b"x");
    assert_eq!(s.get("../evil").as_deref(), Some(&b"x"[..]));

    // The sibling directory of `dir` must remain untouched.
    if let Some(parent) = dir.path().parent() {
        assert!(
            !parent.join("evil").exists(),
            "path traversal reached parent directory"
        );
    }
}

/// `FileStorage::new(None)` uses `$C4_STATES_DIR` when set. Kept in
/// its own test so `env::set_var` cannot leak into the other tests --
/// the whole crate runs with `--test-threads=1` in CI.
#[test]
fn file_storage_honours_c4_states_dir_env() {
    let dir = tempfile::tempdir().expect("tempdir");
    let key = "colibri_env_marker";

    let prev = env::var_os("C4_STATES_DIR");
    env::set_var("C4_STATES_DIR", dir.path());

    let s = FileStorage::new(None).expect("file storage");
    s.set(key, b"env_backed");
    assert_eq!(s.get(key).as_deref(), Some(&b"env_backed"[..]));

    // Restore previous env var to keep other tests hermetic.
    match prev {
        Some(v) => env::set_var("C4_STATES_DIR", v),
        None => env::remove_var("C4_STATES_DIR"),
    }
}
