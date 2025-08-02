"""
Testing utilities and mock implementations for Colibri
"""

import json
from typing import Any, Dict, List, Optional, Union, Callable
from unittest.mock import Mock

from .storage import ColibriStorage
from .types import DataRequest, ColibriError


class MockStorage(ColibriStorage):
    """Mock storage implementation for testing"""

    def __init__(self):
        self._data: Dict[str, bytes] = {}
        self.get_calls: List[str] = []
        self.set_calls: List[tuple[str, bytes]] = []
        self.delete_calls: List[str] = []

    def get(self, key: str) -> Optional[bytes]:
        self.get_calls.append(key)
        return self._data.get(key)

    def set(self, key: str, value: bytes) -> None:
        self.set_calls.append((key, value))
        self._data[key] = value

    def delete(self, key: str) -> None:
        self.delete_calls.append(key)
        self._data.pop(key, None)

    def clear_calls(self) -> None:
        """Clear call history"""
        self.get_calls.clear()
        self.set_calls.clear()
        self.delete_calls.clear()

    def clear_data(self) -> None:
        """Clear stored data"""
        self._data.clear()

    def preset_data(self, data: Dict[str, bytes]) -> None:
        """Preset storage data for testing"""
        self._data.update(data)


class MockRequestHandler:
    """Mock HTTP request handler for testing"""

    def __init__(self):
        self._responses: Dict[str, Any] = {}
        self._method_responses: Dict[str, Dict[str, Any]] = {}
        self._default_response: Optional[bytes] = None
        self.request_calls: List[DataRequest] = []

    def add_response(
        self, 
        method: str, 
        params: List[Any], 
        response: Any,
        as_bytes: bool = False
    ) -> None:
        """
        Add a mock response for a specific method and parameters
        
        Args:
            method: RPC method name
            params: Method parameters
            response: Response data
            as_bytes: If True, return response as bytes
        """
        key = self._make_key(method, params)
        if as_bytes and isinstance(response, str):
            response = response.encode('utf-8')
        elif as_bytes and not isinstance(response, bytes):
            response = json.dumps(response).encode('utf-8')
        
        self._responses[key] = response

    def add_method_response(
        self, 
        method: str, 
        response: Any,
        as_bytes: bool = False
    ) -> None:
        """
        Add a mock response for any call to a specific method
        
        Args:
            method: RPC method name
            response: Response data
            as_bytes: If True, return response as bytes
        """
        if as_bytes and isinstance(response, str):
            response = response.encode('utf-8')
        elif as_bytes and not isinstance(response, bytes):
            response = json.dumps(response).encode('utf-8')
            
        self._method_responses[method] = response

    def set_default_response(self, response: bytes) -> None:
        """Set a default response for unmatched requests"""
        self._default_response = response

    def _make_key(self, method: str, params: List[Any]) -> str:
        """Create a unique key for method + params combination"""
        return f"{method}:{json.dumps(params, sort_keys=True)}"

    async def handle_request(self, request: DataRequest) -> bytes:
        """
        Handle a mock HTTP request
        
        Args:
            request: The data request to handle
            
        Returns:
            Mock response data as bytes
            
        Raises:
            ColibriError: If no mock response is configured
        """
        self.request_calls.append(request)

        # If it's an RPC request with payload, extract method and params
        if request.payload and "method" in request.payload:
            method = request.payload["method"]
            params = request.payload.get("params", [])
            
            # Check for specific method + params response
            key = self._make_key(method, params)
            if key in self._responses:
                response = self._responses[key]
                if isinstance(response, bytes):
                    return response
                return json.dumps(response).encode('utf-8')
            
            # Check for general method response
            if method in self._method_responses:
                response = self._method_responses[method]
                if isinstance(response, bytes):
                    return response
                return json.dumps(response).encode('utf-8')

        # Return default response if configured
        if self._default_response is not None:
            return self._default_response

        # No mock response configured
        raise ColibriError(f"No mock response configured for request: {request.to_dict()}")

    def clear_responses(self) -> None:
        """Clear all configured responses"""
        self._responses.clear()
        self._method_responses.clear()
        self._default_response = None

    def clear_calls(self) -> None:
        """Clear request call history"""
        self.request_calls.clear()

    def get_calls_for_method(self, method: str) -> List[DataRequest]:
        """Get all calls made for a specific RPC method"""
        return [
            call for call in self.request_calls
            if call.payload and call.payload.get("method") == method
        ]


class MockProofData:
    """Helper class for creating mock proof data"""

    @staticmethod
    def create_proof(method: str, params: List[Any], result: Any) -> bytes:
        """Create mock proof bytes for testing"""
        proof_data = {
            "method": method,
            "params": params,
            "result": result,
            "mock": True
        }
        return json.dumps(proof_data).encode('utf-8')

    @staticmethod
    def create_empty_proof() -> bytes:
        """Create empty proof for LOCAL method types"""
        return b""


class TestHelper:
    """Helper class for common testing scenarios"""

    @staticmethod
    def setup_eth_get_balance_mock(
        handler: MockRequestHandler,
        address: str,
        block: str,
        balance: str
    ) -> None:
        """Setup mock for eth_getBalance call"""
        handler.add_response(
            "eth_getBalance",
            [address, block],
            {
                "jsonrpc": "2.0",
                "id": 1,
                "result": balance
            }
        )

    @staticmethod
    def setup_eth_get_block_mock(
        handler: MockRequestHandler,
        block_hash: str,
        block_data: Dict[str, Any]
    ) -> None:
        """Setup mock for eth_getBlockByHash call"""
        handler.add_response(
            "eth_getBlockByHash",
            [block_hash, False],
            {
                "jsonrpc": "2.0",
                "id": 1,
                "result": block_data
            }
        )

    @staticmethod
    def setup_proof_mock(
        handler: MockRequestHandler,
        method: str,
        params: List[Any],
        proof_bytes: Optional[bytes] = None
    ) -> None:
        """Setup mock for proof requests"""
        if proof_bytes is None:
            proof_bytes = MockProofData.create_proof(method, params, "mock_result")
        
        handler.add_response(method, params, proof_bytes, as_bytes=True)

    @staticmethod  
    def create_mock_storage_with_data(data: Dict[str, bytes]) -> MockStorage:
        """Create a mock storage with preset data"""
        storage = MockStorage()
        storage.preset_data(data)
        return storage