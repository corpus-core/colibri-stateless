"""
Storage interfaces and implementations for Colibri
"""

import os
import tempfile
from abc import ABC, abstractmethod
from pathlib import Path
from typing import Dict, Optional, List

from .types import StorageError


class ColibriStorage(ABC):
    """Abstract base class for storage implementations"""

    @abstractmethod
    def get(self, key: str) -> Optional[bytes]:
        """
        Retrieve data for the given key
        
        Args:
            key: The storage key
            
        Returns:
            The stored data as bytes, or None if not found
        """
        pass

    @abstractmethod
    def set(self, key: str, value: bytes) -> None:
        """
        Store data for the given key
        
        Args:
            key: The storage key
            value: The data to store as bytes
        """
        pass

    @abstractmethod
    def delete(self, key: str) -> None:
        """
        Delete data for the given key
        
        Args:
            key: The storage key to delete
        """
        pass


class MemoryStorage(ColibriStorage):
    """In-memory storage implementation for testing"""

    def __init__(self):
        self._data: Dict[str, bytes] = {}

    def get(self, key: str) -> Optional[bytes]:
        return self._data.get(key)

    def set(self, key: str, value: bytes) -> None:
        self._data[key] = value

    def delete(self, key: str) -> None:
        self._data.pop(key, None)

    def clear(self) -> None:
        """Clear all stored data"""
        self._data.clear()

    def size(self) -> int:
        """Get the number of stored items"""
        return len(self._data)


class DefaultStorage(ColibriStorage):
    """Default file-based storage implementation"""

    def __init__(self, base_dir: Optional[str] = None):
        """
        Initialize file storage
        
        Args:
            base_dir: Base directory for storage files. If None, uses C4_STATES_DIR 
                     environment variable or creates a temp directory.
        """
        if base_dir is None:
            base_dir = os.environ.get("C4_STATES_DIR")
            if base_dir is None:
                base_dir = os.path.join(tempfile.gettempdir(), "colibri_states")
        
        self.base_dir = Path(base_dir)
        self.base_dir.mkdir(parents=True, exist_ok=True)
        
    def _get_file_path(self, key: str) -> Path:
        """Get the file path for a storage key"""
        # Sanitize the key to be filesystem-safe
        safe_key = "".join(c for c in key if c.isalnum() or c in "._-")
        if not safe_key:
            safe_key = "empty"
        return self.base_dir / safe_key

    def get(self, key: str) -> Optional[bytes]:
        """Retrieve data from file"""
        try:
            file_path = self._get_file_path(key)
            if file_path.exists():
                return file_path.read_bytes()
            return None
        except (OSError, IOError) as e:
            raise StorageError(f"Failed to read key '{key}'") from e

    def set(self, key: str, value: bytes) -> None:
        """Store data to file"""
        try:
            file_path = self._get_file_path(key)
            file_path.write_bytes(value)
        except (OSError, IOError) as e:
            raise StorageError(f"Failed to write key '{key}'") from e

    def delete(self, key: str) -> None:
        """Delete file"""
        try:
            file_path = self._get_file_path(key)
            if file_path.exists():
                file_path.unlink()
        except (OSError, IOError) as e:
            raise StorageError(f"Failed to delete key '{key}'") from e

    def list_keys(self) -> List[str]:
        """List all stored keys"""
        try:
            return [f.name for f in self.base_dir.iterdir() if f.is_file()]
        except (OSError, IOError) as e:
            raise StorageError("Failed to list keys") from e

    def clear(self) -> None:
        """Clear all stored data"""
        try:
            for file_path in self.base_dir.iterdir():
                if file_path.is_file():
                    file_path.unlink()
        except (OSError, IOError) as e:
            raise StorageError("Failed to clear storage") from e

    def size(self) -> int:
        """Get the number of stored files"""
        try:
            return len([f for f in self.base_dir.iterdir() if f.is_file()])
        except (OSError, IOError):
            return 0