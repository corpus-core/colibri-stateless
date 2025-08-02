"""
Colibri Python Bindings

Python bindings for the Colibri stateless Ethereum proof library.
"""

from .client import Colibri
from .storage import ColibriStorage, DefaultStorage, MemoryStorage  
from .types import MethodType, ColibriError
from .testing import MockStorage, MockRequestHandler

__version__ = "0.1.0"
__author__ = "corpus.core"
__email__ = "contact@corpus.core"

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