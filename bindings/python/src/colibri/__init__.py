"""
Colibri Python Bindings

Python bindings for the Colibri stateless Ethereum proof library.
"""

import atexit
from .storage import ColibriStorage, DefaultStorage, MemoryStorage  
from .types import MethodType, ColibriError
from .testing import MockStorage, MockRequestHandler

# Try to import native module
try:
    from . import _native
except ImportError:
    _native = None

__version__ = "0.1.0"
__author__ = "corpus.core"
__email__ = "contact@corpus.core"

# Global storage management
_global_storage = None
_storage_registered = False

def _register_global_storage(storage: ColibriStorage = None):
    """Register global storage once for the entire module"""
    global _global_storage, _storage_registered
    
    if _storage_registered:
        # Already registered - check if it's the same type
        if storage is not None and type(storage) != type(_global_storage):
            import warnings
            warnings.warn(
                f"Storage already registered as {type(_global_storage).__name__}, "
                f"ignoring {type(storage).__name__}. C storage is global across all instances.",
                RuntimeWarning
            )
        return _global_storage
        
    if storage is None:
        storage = DefaultStorage()
    
    _global_storage = storage
    
    # Register with native module if available
    if _native and hasattr(_native, 'register_storage'):
        _native.register_storage(
            storage.get,
            storage.set,
            storage.delete
        )
        _storage_registered = True
        # Global storage registered successfully
    
    return _global_storage

def _cleanup_global_storage():
    """Cleanup global storage on module exit"""
    global _storage_registered
    if _storage_registered and _native and hasattr(_native, 'clear_storage'):
        try:
            _native.clear_storage()
            _storage_registered = False
            # Global storage cleaned up
        except:
            pass  # Ignore cleanup errors

# Register cleanup handler
atexit.register(_cleanup_global_storage)

# Import client after storage setup to avoid circular imports
from .client import Colibri

__all__ = [
    "Colibri",
    "ColibriStorage", 
    "DefaultStorage",
    "MemoryStorage",
    "MethodType",
    "ColibriError",
    "MockStorage",
    "MockRequestHandler",
]