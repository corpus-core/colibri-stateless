"""
Testing utilities and mock implementations for Colibri Python bindings
"""

import json
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
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


class MockRequestHandler:
    """Mock HTTP request handler for testing"""

    def __init__(self):
        self._responses: Dict[str, Union[bytes, Dict[str, Any]]] = {}
        self.request_calls: List[DataRequest] = []

    async def handle_request(self, request: DataRequest) -> bytes:
        """Handle a mock HTTP request"""
        self.request_calls.append(request)
        return b'{"result": "mock_response"}'


class FileBasedMockStorage(ColibriStorage):
    """Mock storage that loads from test directory files"""
    
    def __init__(self, test_data_dir):
        self.test_data_dir = Path(test_data_dir)
        
    def get(self, key: str) -> Optional[bytes]:
        file_path = self.test_data_dir / key
        if file_path.exists():
            print(f"🗃️ Loading storage: {key}")
            return file_path.read_bytes()
        return None
    
    def set(self, key: str, value: bytes) -> None:
        print(f"🗃️ SET {key} (ignored in mock)")
        pass
    
    def delete(self, key: str) -> None:
        print(f"🗃️ DELETE {key} (ignored in mock)")
        pass


class FileBasedMockRequestHandler:
    """Mock request handler that loads from test directory files"""
    
    def __init__(self, test_data_dir):
        self.test_data_dir = Path(test_data_dir)
    
    async def handle_request(self, request: DataRequest) -> bytes:
        """Handle mock HTTP request by loading from file"""
        
        # Convert request to filename (simplified)
        if request.url:
            filename = request.url.replace('/', '_').replace('?', '_').replace('=', '_').replace('&', '_')
            filename = filename + '.' + request.encoding
        elif request.payload and 'method' in request.payload:
            method = request.payload['method']
            filename = method + '.' + request.encoding
        else:
            filename = 'unknown.' + request.encoding
        
        print(f"📡 Looking for mock file: {filename}")
        
        file_path = self.test_data_dir / filename
        if file_path.exists():
            data = file_path.read_bytes()
            print(f"📡 Found mock response ({len(data)} bytes)")
            return data
        
        # Enhanced fallback logic for light_client_updates specifically
        if 'light_client_updates' in filename:
            print(f"📡 Applying light_client_updates fallback")
            # Find any light_client_updates file in the directory
            pattern = "*light_client_updates*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                # Choose the first available light client update file
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                print(f"📡 Found light_client fallback: {fallback_file.name} ({len(data)} bytes)")
                return data
        
        # Beacon headers fallback
        if 'beacon/headers' in request.url:
            pattern = "*headers*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                print(f"📡 Found headers fallback: {fallback_file.name} ({len(data)} bytes)")
                return data
        
        # Beacon blocks fallback  
        if 'beacon/blocks' in request.url:
            pattern = "*blocks*"
            matching_files = list(self.test_data_dir.glob(pattern))
            if matching_files:
                fallback_file = matching_files[0]
                data = fallback_file.read_bytes()
                print(f"📡 Found blocks fallback: {fallback_file.name} ({len(data)} bytes)")
                return data
        
        # List available files for debugging
        available_files = [f.name for f in self.test_data_dir.iterdir() if f.is_file()]
        print(f"📋 Available files: {available_files}")
        
        raise Exception(f"No mock response file found for: {filename}")


def discover_tests(test_data_root=None):
    """Discover test cases from test/data directories"""
    
    if test_data_root is None:
        current_dir = Path(__file__).parent
        test_data_root = current_dir / '..' / '..' / '..' / '..' / 'test' / 'data'
    
    test_data_root = Path(test_data_root).resolve()
    print(f"🔍 Discovering tests in: {test_data_root}")
    
    if not test_data_root.exists():
        print(f"❌ Test data directory not found: {test_data_root}")
        return []
    
    test_cases = []
    
    for test_json_path in test_data_root.glob('*/test.json'):
        test_dir = test_json_path.parent
        test_name = test_dir.name
        
        try:
            with open(test_json_path, 'r') as f:
                test_config = json.load(f)
            
            test_case = {
                'name': test_name,
                'directory': test_dir,
                'method': test_config['method'],
                'params': test_config['params'],
                'chain_id': test_config['chain_id'],
                'expected_result': test_config.get('expected_result')
            }
            
            test_cases.append(test_case)
            print(f"✅ Found test: {test_name} - {test_config['method']}")
            
        except (json.JSONDecodeError, KeyError) as e:
            print(f"❌ Invalid test.json in {test_dir}: {e}")
            continue
    
    print(f"�� Discovered {len(test_cases)} test cases")
    return test_cases


async def run_test_case(test_case):
    """Run a single test case"""
    
    from . import Colibri
    
    test_name = test_case['name']
    test_dir = test_case['directory']
    method = test_case['method']
    params = test_case['params']
    chain_id = test_case['chain_id']
    expected_result = test_case.get('expected_result')
    
    print(f"\n🧪 Running test: {test_name}")
    print(f"   Method: {method}")
    print(f"   Chain ID: {chain_id}")
    
    # Create mocks
    mock_storage = FileBasedMockStorage(test_dir)
    mock_request_handler = FileBasedMockRequestHandler(test_dir)
    
    # Create client
    client = Colibri(
        chain_id=chain_id,
        storage=mock_storage,
        request_handler=mock_request_handler
    )
    
    try:
        result = await client.rpc(method, params)
        
        print(f"✅ Test completed: {result}")
        
        # Compare with expected if available
        if expected_result is not None:
            if result == expected_result:
                print(f"✅ Result matches expected")
                return {'status': 'PASSED', 'name': test_name, 'result': result}
            else:
                print(f"❌ Result mismatch! Expected {expected_result}, got {result}")
                return {'status': 'FAILED', 'name': test_name, 'error': 'Result mismatch', 'result': result}
        else:
            print(f"ℹ️ No expected result to compare")
            return {'status': 'PASSED', 'name': test_name, 'result': result}
        
    except Exception as e:
        print(f"❌ Test error: {e}")
        return {'status': 'ERROR', 'name': test_name, 'error': str(e)}
