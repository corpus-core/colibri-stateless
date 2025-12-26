pub(crate) mod ffi;

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::fs;
use std::env;

/// Trait for storage implementations
pub trait Storage: Send + Sync {
    /// Get data for a key
    fn get(&self, key: &str) -> Option<Vec<u8>>;

    /// Set data for a key
    fn set(&self, key: &str, value: &[u8]);

    /// Delete data for a key
    fn delete(&self, key: &str);
}

/// In-memory storage implementation
pub struct MemoryStorage {
    data: Arc<Mutex<HashMap<String, Vec<u8>>>>,
}

impl MemoryStorage {
    /// Create a new memory storage
    pub fn new() -> Box<Self> {
        Box::new(Self {
            data: Arc::new(Mutex::new(HashMap::new())),
        })
    }

    /// Create a new memory storage (not boxed)
    pub fn new_unboxed() -> Self {
        Self {
            data: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Clear all stored data
    pub fn clear(&self) {
        self.data.lock().unwrap().clear();
    }

    /// Get the number of stored items
    pub fn size(&self) -> usize {
        self.data.lock().unwrap().len()
    }
}

impl Storage for MemoryStorage {
    fn get(&self, key: &str) -> Option<Vec<u8>> {
        self.data.lock().unwrap().get(key).cloned()
    }

    fn set(&self, key: &str, value: &[u8]) {
        self.data.lock().unwrap().insert(key.to_string(), value.to_vec());
    }

    fn delete(&self, key: &str) {
        self.data.lock().unwrap().remove(key);
    }
}

/// File-based storage implementation
pub struct FileStorage {
    base_dir: PathBuf,
}

impl FileStorage {
    /// Create a new file storage
    pub fn new(base_dir: Option<PathBuf>) -> std::io::Result<Self> {
        let base_dir = base_dir.unwrap_or_else(|| {
            env::var("C4_STATES_DIR")
                .map(PathBuf::from)
                .unwrap_or_else(|_| {
                    env::temp_dir().join("colibri_states")
                })
        });

        fs::create_dir_all(&base_dir)?;

        Ok(Self { base_dir })
    }

    /// Get the file path for a storage key
    fn get_file_path(&self, key: &str) -> PathBuf {
        // Sanitize the key to be filesystem-safe
        let safe_key: String = key.chars()
            .filter(|c| c.is_alphanumeric() || *c == '.' || *c == '_' || *c == '-')
            .collect();

        let safe_key = if safe_key.is_empty() {
            "empty".to_string()
        } else {
            safe_key
        };

        self.base_dir.join(format!("{}.dat", safe_key))
    }
}

impl Storage for FileStorage {
    fn get(&self, key: &str) -> Option<Vec<u8>> {
        let path = self.get_file_path(key);
        fs::read(path).ok()
    }

    fn set(&self, key: &str, value: &[u8]) {
        let path = self.get_file_path(key);
        let _ = fs::write(path, value);
    }

    fn delete(&self, key: &str) {
        let path = self.get_file_path(key);
        let _ = fs::remove_file(path);
    }
}

/// Default storage type
pub type DefaultStorage = FileStorage;

/// Create default storage
pub fn default_storage() -> std::io::Result<DefaultStorage> {
    FileStorage::new(None)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_memory_storage() {
        let storage = MemoryStorage::new_unboxed();

        // Test set and get
        storage.set("key1", b"value1");
        assert_eq!(storage.get("key1"), Some(b"value1".to_vec()));

        // Test update
        storage.set("key1", b"value2");
        assert_eq!(storage.get("key1"), Some(b"value2".to_vec()));

        // Test delete
        storage.delete("key1");
        assert_eq!(storage.get("key1"), None);

        // Test size
        storage.set("a", b"1");
        storage.set("b", b"2");
        assert_eq!(storage.size(), 2);

        // Test clear
        storage.clear();
        assert_eq!(storage.size(), 0);
    }

    #[test]
    fn test_file_storage() {
        let temp_dir = env::temp_dir().join("colibri_test_storage");
        let _ = fs::remove_dir_all(&temp_dir);

        let storage = FileStorage::new(Some(temp_dir.clone())).unwrap();

        // Test set and get
        storage.set("test_key", b"test_value");
        assert_eq!(storage.get("test_key"), Some(b"test_value".to_vec()));

        // Test update
        storage.set("test_key", b"new_value");
        assert_eq!(storage.get("test_key"), Some(b"new_value".to_vec()));

        // Test delete
        storage.delete("test_key");
        assert_eq!(storage.get("test_key"), None);

        // Cleanup
        let _ = fs::remove_dir_all(&temp_dir);
    }

    #[test]
    fn test_file_path_sanitization() {
        let temp_dir = env::temp_dir().join("colibri_test_sanitize");
        let storage = FileStorage::new(Some(temp_dir.clone())).unwrap();

        // Test unsafe characters are sanitized
        storage.set("key/with\\bad:chars", b"data");
        assert_eq!(storage.get("key/with\\bad:chars"), Some(b"data".to_vec()));

        // Cleanup
        let _ = fs::remove_dir_all(&temp_dir);
    }
}