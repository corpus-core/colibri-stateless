"""
Testing utilities and mock implementations for Colibri Python bindings
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
from unittest.mock import Mock

from .storage import ColibriStorage
from .types import DataRequest, ColibriError


class MockProofData:
    """Utility class for creating mock proof data for testing"""
    
    @staticmethod
    def create_proof(method: str, params: List[Any], result: Any) -> bytes:
        """
        Create mock proof data for a given method, params, and result.
        
        @param method The RPC method name
        @param params The RPC method parameters
        @param result The expected result
        @return Mock proof data as bytes
        """
        proof_dict = {
            "method": method,
            "params": params,
            "result": result,
            "mock": True
        }
        return json.dumps(proof_dict).encode('utf-8')
    
    @staticmethod
    def create_empty_proof() -> bytes:
        """
        Create an empty proof (used for LOCAL methods).
        
        @return Empty bytes
        """
        return b""


class TestHelper:
    """Helper utilities for setting up test scenarios"""
    
    @staticmethod
    def setup_eth_get_balance_mock(
        handler: 'MockRequestHandler',
        address: str,
        block: str,
        balance: str
    ) -> None:
        """
        Setup a mock response for eth_getBalance.
        
        @param handler The mock request handler
        @param address The Ethereum address
        @param block The block number or tag
        @param balance The balance to return (hex string)
        """
        response = {
            "jsonrpc": "2.0",
            "id": 1,
            "result": balance
        }
        handler.add_response("eth_getBalance", [address, block], response)
    
    @staticmethod
    def setup_eth_get_block_mock(
        handler: 'MockRequestHandler',
        block_hash: str,
        block_data: Dict[str, Any]
    ) -> None:
        """
        Setup a mock response for eth_getBlockByHash.
        
        @param handler The mock request handler
        @param block_hash The block hash
        @param block_data The block data to return
        """
        response = {
            "jsonrpc": "2.0",
            "id": 1,
            "result": block_data
        }
        handler.add_response("eth_getBlockByHash", [block_hash, False], response)
    
    @staticmethod
    def setup_proof_mock(
        handler: 'MockRequestHandler',
        method: str,
        params: List[Any],
        proof_data: Optional[bytes] = None
    ) -> None:
        """
        Setup a mock proof response for a method.
        
        @param handler The mock request handler
        @param method The RPC method name
        @param params The RPC method parameters
        @param proof_data Optional custom proof data (generates default if None)
        """
        if proof_data is None:
            proof_data = MockProofData.create_proof(method, params, "mock_result")
        handler.add_response(method, params, proof_data)
    
    @staticmethod
    def create_mock_storage_with_data(preset_data: Dict[str, bytes]) -> MockStorage:
        """
        Create a MockStorage instance with preset data.
        
        @param preset_data Dictionary of key-value pairs to preset in storage
        @return MockStorage instance with preset data
        """
        storage = MockStorage()
        for key, value in preset_data.items():
            storage.set(key, value)
        return storage


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

    def size(self) -> int:
        """Return the number of items in storage"""
        return len(self._data)

    def preset_data(self, data: Dict[str, bytes]) -> None:
        """
        Preset storage with initial data without tracking calls.
        
        @param data Dictionary of key-value pairs to preset
        """
        self._data.update(data)

    def clear_data(self) -> None:
        """Clear all data from storage without tracking calls"""
        self._data.clear()

    def clear_calls(self) -> None:
        """Clear the history of tracked calls"""
        self.get_calls.clear()
        self.set_calls.clear()
        self.delete_calls.clear()


class MockRequestHandler:
    """Mock HTTP request handler for testing"""

    def __init__(self):
        self._responses: Dict[str, Union[bytes, Dict[str, Any]]] = {}
        self._method_responses: Dict[str, Union[bytes, Dict[str, Any]]] = {}
        self._default_response: Optional[bytes] = None
        self.request_calls: List[DataRequest] = []

    def _make_key(self, method: str, params: List[Any]) -> str:
        """Create a unique key for method + params combination"""
        return f"{method}:{json.dumps(params, sort_keys=True)}"

    def add_response(
        self, 
        method: str, 
        params: List[Any], 
        response: Union[bytes, Dict[str, Any], str]
    ) -> None:
        """
        Add a mock response for a specific method and params combination.
        
        @param method The RPC method name
        @param params The RPC method parameters
        @param response The response to return (can be bytes, dict, or string)
        """
        key = self._make_key(method, params)
        self._responses[key] = response

    def add_method_response(
        self,
        method: str,
        response: Union[bytes, Dict[str, Any], str]
    ) -> None:
        """
        Add a mock response for any call to a method (ignoring params).
        
        @param method The RPC method name
        @param response The response to return
        """
        self._method_responses[method] = response

    def set_default_response(self, response: bytes) -> None:
        """
        Set a default response for any unmatched requests.
        
        @param response The default response to return
        """
        self._default_response = response

    def clear_responses(self) -> None:
        """Clear all configured responses"""
        self._responses.clear()
        self._method_responses.clear()
        self._default_response = None

    def clear_calls(self) -> None:
        """Clear the history of request calls"""
        self.request_calls.clear()

    def get_calls_for_method(self, method: str) -> List[DataRequest]:
        """
        Get all request calls for a specific method.
        
        @param method The RPC method name
        @return List of matching requests
        """
        return [
            req for req in self.request_calls
            if req.payload and req.payload.get("method") == method
        ]

    async def handle_request(self, request: DataRequest) -> bytes:
        """
        Handle a mock HTTP request.
        
        @param request The data request to handle
        @return Mock response data
        """
        self.request_calls.append(request)
        
        # Try to find a matching response
        if request.payload and "method" in request.payload:
            method = request.payload["method"]
            params = request.payload.get("params", [])
            
            # First try exact match (method + params)
            key = self._make_key(method, params)
            if key in self._responses:
                response = self._responses[key]
                if isinstance(response, bytes):
                    return response
                return json.dumps(response).encode('utf-8')
            
            # Then try method-only match
            if method in self._method_responses:
                response = self._method_responses[method]
                if isinstance(response, bytes):
                    return response
                return json.dumps(response).encode('utf-8')
        
        # Use default response if available
        if self._default_response is not None:
            return self._default_response
        
        # No response configured
        raise ColibriError(
            f"No mock response configured for request: "
            f"method={request.payload.get('method') if request.payload else 'unknown'}"
        )


class FileBasedMockStorage(ColibriStorage):
    """Mock storage that loads from test directory files with caching to prevent loops"""
    
    def __init__(self, test_data_dir):
        self.test_data_dir = Path(test_data_dir)
        self._cache: Dict[str, Optional[bytes]] = {}
        self._access_count: Dict[str, int] = {}
        self._max_access_per_key = 5  # Prevent infinite loops
    
    def _find_file_with_truncation(self, filename: str) -> Optional[Path]:
        """
        Find a file, handling filesystem truncation (macOS has 255 char limit).
        
        @param filename The full filename to search for
        @return The path to the file if found, None otherwise
        """
        file_path = self.test_data_dir / filename
        
        # Try exact match first
        if file_path.exists():
            return file_path
        
        # If not found and filename is long, try to find truncated versions
        if len(filename) > 200:
            # Get the extension
            parts = filename.rsplit('.', 1)
            if len(parts) == 2:
                base_name, extension = parts
                
                # Search for files that start with the same prefix and have same extension
                for prefix_len in [250, 240, 230, 220, 200, 150, 100]:
                    if len(base_name) > prefix_len:
                        prefix = base_name[:prefix_len]
                        pattern = f"{prefix}*.{extension}"
                        matching_files = list(self.test_data_dir.glob(pattern))
                        if matching_files:
                            return matching_files[0]
            else:
                # No extension, just search by prefix
                for prefix_len in [250, 240, 230, 220, 200, 150, 100]:
                    if len(filename) > prefix_len:
                        prefix = filename[:prefix_len]
                        pattern = f"{prefix}*"
                        matching_files = list(self.test_data_dir.glob(pattern))
                        if matching_files:
                            return matching_files[0]
        
        return None
        
    def get(self, key: str) -> Optional[bytes]:
        # CRITICAL: Return None immediately to break infinite loops

        # Track access count to prevent infinite loops
        self._access_count[key] = self._access_count.get(key, 0) + 1
        
        if self._access_count[key] > self._max_access_per_key:
            # Storage access limit reached - return None
            return self._cache.get(key)
        
        # Check cache first
        if key in self._cache:
            # Return cached value
            return self._cache[key]
        
        # Load from file (with truncation handling)
        file_path = self._find_file_with_truncation(key)
        if file_path:
            data = file_path.read_bytes()
            # Load file from storage
            self._cache[key] = data
            return data
        else:
            # Storage file not found
            self._cache[key] = None
            return None
    
    def set(self, key: str, value: bytes) -> None:
        # Cache value in mock storage
        self._cache[key] = value
    
    def delete(self, key: str) -> None:
        # Remove from cache
        self._cache.pop(key, None)


class FileBasedMockRequestHandler:
    """Mock request handler that loads from test directory files"""
    
    def __init__(self, test_data_dir):
        self.test_data_dir = Path(test_data_dir)
        self._request_count = 0
        self._max_requests = 50  # Prevent infinite request loops
    
    def _find_file_with_truncation(self, filename: str) -> Optional[Path]:
        """
        Find a file, handling filesystem truncation (macOS has 255 char limit).
        
        @param filename The full filename to search for
        @return The path to the file if found, None otherwise
        """
        file_path = self.test_data_dir / filename
        
        # Try exact match first
        if file_path.exists():
            return file_path
        
        # If not found and filename is long, try to find truncated versions
        # macOS typically truncates at 255 characters
        if len(filename) > 200:  # If it's potentially truncated
            # Get the extension
            parts = filename.rsplit('.', 1)
            if len(parts) == 2:
                base_name, extension = parts
                
                # Search for files that start with the same prefix and have same extension
                # Use progressively shorter prefixes to find the truncated file
                for prefix_len in [250, 240, 230, 220, 200, 150, 100]:
                    if len(base_name) > prefix_len:
                        prefix = base_name[:prefix_len]
                        pattern = f"{prefix}*.{extension}"
                        matching_files = list(self.test_data_dir.glob(pattern))
                        if matching_files:
                            # Return the first match
                            return matching_files[0]
        
        return None
    
    async def handle_request(self, request: DataRequest) -> bytes:
        """Handle mock HTTP request by loading from file"""
        
        self._request_count += 1
        if self._request_count > self._max_requests:
            raise Exception(f"Too many requests ({self._request_count}) - possible infinite loop")
        
        # Convert request to filename base (without extension) - matching C implementation
        if request.url:
            # Sanitize URL to create filename base
            base_name = request.url
            # Replace problematic characters with underscore (matching C implementation)
            for char in ['/', '.', ',', ' ', ':', '=', '?', '"', '&', '[', ']', '{', '}']:
                base_name = base_name.replace(char, '_')
        elif request.payload and 'method' in request.payload:
            # For RPC requests, use method name and parameters
            import json
            method = request.payload.get('method', '')
            params = request.payload.get('params', [])
            base_name = method
            for param in params:
                param_str = param if isinstance(param, str) else json.dumps(param)
                base_name += '_' + param_str
            # Sanitize the base name
            for char in ['/', '.', ',', ' ', ':', '=', '?', '"', '&', '[', ']', '{', '}']:
                base_name = base_name.replace(char, '_')
        else:
            base_name = 'unknown'
        
        # CRITICAL: Truncate to maximum length BEFORE adding extension (matching C: C4_MAX_MOCKNAME_LEN = 100)
        MAX_MOCKNAME_LEN = 100
        if len(base_name) > MAX_MOCKNAME_LEN:
            base_name = base_name[:MAX_MOCKNAME_LEN]
        
        # Add file extension based on encoding type
        filename = base_name + '.' + request.encoding
        
        # Look for mock response file (with truncation handling)
        file_path = self._find_file_with_truncation(filename)
        if file_path:
            data = file_path.read_bytes()
            # Found mock response file
            return data
        
        # Enhanced fallback logic for light_client_updates specifically
        if 'light_client_updates' in filename:
            # Try light_client_updates fallback
            # Find any light_client_updates file in the directory
            pattern = "*light_client_updates*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                # Choose the first available light client update file
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                # Found light_client fallback
                return data
        
        # Beacon headers fallback
        if 'beacon/headers' in request.url:
            pattern = "*headers*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                # Found headers fallback
                return data
        
        # Beacon blocks fallback  
        if 'beacon/blocks' in request.url:
            pattern = "*blocks*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                # Found blocks fallback
                return data
        
        # List available files for debugging
        available_files = [f.name for f in self.test_data_dir.iterdir() if f.is_file()]
        print(f"Available files: {available_files[:10]}...")  # Limit output
        
        raise Exception(f"No mock response file found for: {filename}")


def discover_tests(test_data_root=None):
    """Discover test cases from test/data directories, skipping those with requires_chain_store"""
    
    if test_data_root is None:
        current_dir = Path(__file__).parent
        test_data_root = current_dir / '..' / '..' / '..' / '..' / 'test' / 'data'
    
    test_data_root = Path(test_data_root).resolve()
    # Discover tests in test data directory
    
    if not test_data_root.exists():
        print(f"Test data directory not found: {test_data_root}")
        return []
    
    test_cases = []
    skipped_cases = []
    
    for test_json_path in test_data_root.glob('*/test.json'):
        test_dir = test_json_path.parent
        test_name = test_dir.name
        
        try:
            with open(test_json_path, 'r') as f:
                test_config = json.load(f)
            
            # Skip tests that require chain store
            if test_config.get('requires_chain_store', False):
                skipped_cases.append(test_name)
                print(f"Skipping test (requires_chain_store): {test_name}")
                continue
            
            test_case = {
                'name': test_name,
                'directory': test_dir,
                'method': test_config['method'],
                'params': test_config['params'],
                'chain_id': test_config['chain_id'],
                'expected_result': test_config.get('expected_result')
            }
            
            test_cases.append(test_case)
            print(f"Found test: {test_name} - {test_config['method']}")
            
        except (json.JSONDecodeError, KeyError) as e:
            print(f"Invalid test.json in {test_dir}: {e}")
            continue
    
    print(f"Discovered {len(test_cases)} test cases")
    if skipped_cases:
        print(f"Skipped {len(skipped_cases)} tests requiring chain store: {', '.join(skipped_cases[:5])}{'...' if len(skipped_cases) > 5 else ''}")
    return test_cases


async def run_test_case(test_case):
    """Run a single test case"""
    
    # Lazy import to avoid circular import issues
    from .client import Colibri
    
    test_name = test_case['name']
    test_dir = test_case['directory']
    method = test_case['method']
    params = test_case['params']
    chain_id = test_case['chain_id']
    expected_result = test_case.get('expected_result')
    
    print(f"\nRunning test: {test_name}")
    print(f"   Method: {method}")
    print(f"   Chain ID: {chain_id}")
    
    # Create mocks with loop prevention
    mock_storage = FileBasedMockStorage(test_dir)
    mock_request_handler = FileBasedMockRequestHandler(test_dir)
    
    # Create client with NO provers to force local proof creation
    client = Colibri(
        chain_id=chain_id,
        provers=[],  # CRITICAL: No remote provers! Use only mock data
        storage=mock_storage,
        request_handler=mock_request_handler
    )
    
    try:
        result = await client.rpc(method, params)
        
        print(f"Test completed: {result}")
        
        # Compare with expected if available
        if expected_result is not None:
            if result == expected_result:
                print(f"Result matches expected")
                return {'status': 'PASSED', 'name': test_name, 'result': result}
            else:
                print(f"Result mismatch! Expected {expected_result}, got {result}")
                return {'status': 'FAILED', 'name': test_name, 'error': 'Result mismatch', 'result': result}
        else:
            print(f"No expected result to compare")
            return {'status': 'PASSED', 'name': test_name, 'result': result}
        
    except Exception as e:
        print(f"Test error: {e}")
        return {'status': 'ERROR', 'name': test_name, 'error': str(e)}