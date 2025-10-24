"""
Tests for Colibri storage implementations
"""

import pytest
import tempfile
import os
from pathlib import Path

from colibri.storage import ColibriStorage, MemoryStorage, DefaultStorage
from colibri.testing import MockStorage
from colibri.types import StorageError


class TestMemoryStorage:
    """Test the in-memory storage implementation"""
    
    def test_memory_storage_basic_operations(self):
        """Test basic get/set/delete operations"""
        storage = MemoryStorage()
        
        # Test empty get
        assert storage.get("nonexistent") is None
        
        # Test set and get
        test_data = b"hello world"
        storage.set("test_key", test_data)
        assert storage.get("test_key") == test_data
        
        # Test overwrite
        new_data = b"goodbye world"
        storage.set("test_key", new_data)
        assert storage.get("test_key") == new_data
        
        # Test delete
        storage.delete("test_key")
        assert storage.get("test_key") is None
        
        # Test delete nonexistent (should not raise)
        storage.delete("nonexistent")
    
    def test_memory_storage_multiple_keys(self):
        """Test storage with multiple keys"""
        storage = MemoryStorage()
        
        data1 = b"data1"
        data2 = b"data2"
        data3 = b"data3"
        
        storage.set("key1", data1)
        storage.set("key2", data2)
        storage.set("key3", data3)
        
        assert storage.get("key1") == data1
        assert storage.get("key2") == data2
        assert storage.get("key3") == data3
        
        assert storage.size() == 3
        
        storage.delete("key2")
        assert storage.get("key2") is None
        assert storage.size() == 2
        
        storage.clear()
        assert storage.size() == 0
        assert storage.get("key1") is None
        assert storage.get("key3") is None


class TestDefaultStorage:
    """Test the file-based storage implementation"""
    
    def test_default_storage_basic_operations(self):
        """Test basic file storage operations"""
        with tempfile.TemporaryDirectory() as temp_dir:
            storage = DefaultStorage(temp_dir)
            
            # Test empty get
            assert storage.get("nonexistent") is None
            
            # Test set and get
            test_data = b"hello file world"
            storage.set("test_key", test_data)
            assert storage.get("test_key") == test_data
            
            # Verify file was created
            assert (Path(temp_dir) / "test_key").exists()
            
            # Test overwrite
            new_data = b"goodbye file world"
            storage.set("test_key", new_data)
            assert storage.get("test_key") == new_data
            
            # Test delete
            storage.delete("test_key")
            assert storage.get("test_key") is None
            assert not (Path(temp_dir) / "test_key").exists()
    
    def test_default_storage_multiple_keys(self):
        """Test file storage with multiple keys"""
        with tempfile.TemporaryDirectory() as temp_dir:
            storage = DefaultStorage(temp_dir)
            
            data1 = b"file_data1"
            data2 = b"file_data2"
            data3 = b"file_data3"
            
            storage.set("file_key1", data1)
            storage.set("file_key2", data2)
            storage.set("file_key3", data3)
            
            assert storage.get("file_key1") == data1
            assert storage.get("file_key2") == data2
            assert storage.get("file_key3") == data3
            
            keys = storage.list_keys()
            assert "file_key1" in keys
            assert "file_key2" in keys
            assert "file_key3" in keys
            assert storage.size() == 3
            
            storage.delete("file_key2")
            assert storage.get("file_key2") is None
            assert storage.size() == 2
            
            storage.clear()
            assert storage.size() == 0
            assert storage.get("file_key1") is None
    
    def test_default_storage_with_env_var(self):
        """Test DefaultStorage with C4_STATES_DIR environment variable"""
        with tempfile.TemporaryDirectory() as temp_dir:
            # Set environment variable
            old_env = os.environ.get("C4_STATES_DIR")
            os.environ["C4_STATES_DIR"] = temp_dir
            
            try:
                storage = DefaultStorage()  # Should use env var
                
                test_data = b"env var test"
                storage.set("env_test", test_data)
                
                # Verify it was written to the env var directory
                assert (Path(temp_dir) / "env_test").exists()
                assert storage.get("env_test") == test_data
                
            finally:
                # Restore environment
                if old_env is not None:
                    os.environ["C4_STATES_DIR"] = old_env
                else:
                    os.environ.pop("C4_STATES_DIR", None)
    
    def test_default_storage_key_sanitization(self):
        """Test that keys are properly sanitized for filesystem"""
        with tempfile.TemporaryDirectory() as temp_dir:
            storage = DefaultStorage(temp_dir)
            
            # Test keys with special characters
            unsafe_key = "test/key:with*special?chars"
            test_data = b"sanitized key test"
            
            # Should not raise an exception
            storage.set(unsafe_key, test_data)
            assert storage.get(unsafe_key) == test_data
            
            # Verify the file exists with a sanitized name
            files = list(Path(temp_dir).glob("*"))
            assert len(files) == 1
            # The exact sanitized name depends on implementation


class TestMockStorage:
    """Test the mock storage implementation for testing"""
    
    def test_mock_storage_call_tracking(self):
        """Test that mock storage tracks calls"""
        storage = MockStorage()
        
        # Initially no calls
        assert len(storage.get_calls) == 0
        assert len(storage.set_calls) == 0
        assert len(storage.delete_calls) == 0
        
        # Perform operations
        storage.get("test_key")
        storage.set("test_key", b"test_data")
        storage.get("test_key")  # Get again to verify the set worked
        storage.delete("test_key")
        
        # Check calls were tracked
        assert storage.get_calls == ["test_key", "test_key"]  # Two gets: before and after set
        assert storage.set_calls == [("test_key", b"test_data")]
        assert storage.delete_calls == ["test_key"]
        
        # Clear call history
        storage.clear_calls()
        assert len(storage.get_calls) == 0
        assert len(storage.set_calls) == 0
        assert len(storage.delete_calls) == 0
    
    def test_mock_storage_preset_data(self):
        """Test setting preset data in mock storage"""
        storage = MockStorage()
        
        preset_data = {
            "key1": b"value1",
            "key2": b"value2"
        }
        storage.preset_data(preset_data)
        
        assert storage.get("key1") == b"value1"
        assert storage.get("key2") == b"value2"
        assert storage.get("key3") is None
    
    def test_mock_storage_data_clearing(self):
        """Test clearing data in mock storage"""
        storage = MockStorage()
        
        storage.set("key1", b"value1")
        storage.set("key2", b"value2")
        
        assert storage.get("key1") == b"value1"
        assert storage.get("key2") == b"value2"
        
        storage.clear_data()
        
        assert storage.get("key1") is None
        assert storage.get("key2") is None
        
        # Calls should still be tracked
        assert len(storage.get_calls) > 0