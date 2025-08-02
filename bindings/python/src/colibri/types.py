"""
Type definitions for Colibri Python bindings
"""

from enum import IntEnum
from typing import Any, Dict, List, Optional


class MethodType(IntEnum):
    """Enum for RPC method support types"""
    UNKNOWN = 0
    PROOFABLE = 1
    UNPROOFABLE = 2  
    NOT_SUPPORTED = 3
    LOCAL = 4

    def __str__(self) -> str:
        return self.name.lower()

    @property
    def description(self) -> str:
        """Human-readable description of the method type"""
        descriptions = {
            MethodType.UNKNOWN: "Unknown method type",
            MethodType.PROOFABLE: "Method supports proof generation",
            MethodType.UNPROOFABLE: "Method doesn't support proofs, direct RPC call",
            MethodType.NOT_SUPPORTED: "Method not supported",
            MethodType.LOCAL: "Local verification only",
        }
        return descriptions.get(self, "Unknown")


class ColibriError(Exception):
    """Base exception class for Colibri errors"""
    
    def __init__(self, message: str, details: Optional[str] = None):
        super().__init__(message)
        self.message = message
        self.details = details

    def __str__(self) -> str:
        if self.details:
            return f"{self.message}: {self.details}"
        return self.message


class ProofError(ColibriError):
    """Exception raised during proof generation"""
    pass


class VerificationError(ColibriError):
    """Exception raised during proof verification"""
    pass


class RPCError(ColibriError):
    """Exception raised during RPC calls"""
    
    def __init__(self, message: str, code: Optional[int] = None, details: Optional[str] = None):
        super().__init__(message, details)
        self.code = code


class HTTPError(ColibriError):
    """Exception raised for HTTP errors"""
    
    def __init__(self, message: str, status_code: Optional[int] = None, details: Optional[str] = None):
        super().__init__(message, details)
        self.status_code = status_code


class StorageError(ColibriError):
    """Exception raised for storage operations"""
    pass


class DataRequest:
    """Represents a data request from the C library"""
    
    def __init__(
        self,
        req_ptr: int,
        url: str,
        method: str,
        payload: Optional[Dict[str, Any]] = None,
        encoding: str = "json",
        request_type: str = "eth_rpc",
        exclude_mask: int = 0,
        chain_id: int = 1,
    ):
        self.req_ptr = req_ptr
        self.url = url
        self.method = method
        self.payload = payload
        self.encoding = encoding
        self.request_type = request_type
        self.exclude_mask = exclude_mask
        self.chain_id = chain_id

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "DataRequest":
        """Create DataRequest from dictionary returned by C library"""
        return cls(
            req_ptr=int(data["req_ptr"]),
            url=data.get("url", ""),
            method=data.get("method", "GET"),
            payload=data.get("payload"),
            encoding=data.get("encoding", "json"),
            request_type=data.get("type", "eth_rpc"),
            exclude_mask=int(data.get("exclude_mask", 0)),
            chain_id=int(data.get("chain_id", 1)),
        )

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for serialization"""
        result = {
            "req_ptr": self.req_ptr,
            "url": self.url,
            "method": self.method,
            "encoding": self.encoding,
            "type": self.request_type,
            "exclude_mask": self.exclude_mask,
            "chain_id": self.chain_id,
        }
        if self.payload is not None:
            result["payload"] = self.payload
        return result


# Chain configuration types
ChainConfig = Dict[str, Any]
ServerList = List[str]
TrustedBlockHashes = List[str]