"""
Tests for Colibri testing utilities
"""

import pytest
import json

from colibri.testing import MockStorage, MockRequestHandler, MockProofData, TestHelper
from colibri.types import DataRequest, ColibriError


class TestMockRequestHandler:
    """Test the mock request handler"""
    
    def test_mock_request_handler_basic(self):
        """Test basic mock request handler functionality"""
        handler = MockRequestHandler()
        
        # Initially no calls
        assert len(handler.request_calls) == 0
        
        # Add a response
        handler.add_response("eth_getBalance", ["0x123", "latest"], "0x1000")
        
        # Create a test request
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={
                "method": "eth_getBalance",
                "params": ["0x123", "latest"]
            }
        )
        
        # Test the handler (this would normally be called async)
        # We'll test the response matching logic directly
        method = request.payload["method"]
        params = request.payload["params"]
        key = handler._make_key(method, params)
        
        assert key in handler._responses
        assert handler._responses[key] == "0x1000"
    
    def test_mock_request_handler_method_responses(self):
        """Test method-level responses (ignoring params)"""
        handler = MockRequestHandler()
        
        # Add method-level response
        handler.add_method_response("eth_chainId", "0x1")
        
        request1 = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={"method": "eth_chainId", "params": []}
        )
        
        request2 = DataRequest(
            req_ptr=12346,
            url="",
            method="POST",
            payload={"method": "eth_chainId", "params": ["extra_param"]}
        )
        
        # Both should match the method response
        assert "eth_chainId" in handler._method_responses
        assert handler._method_responses["eth_chainId"] == "0x1"
    
    @pytest.mark.asyncio
    async def test_mock_request_handler_call_tracking(self):
        """Test that calls are properly tracked"""
        handler = MockRequestHandler()
        
        handler.add_response("eth_getBalance", ["0x123", "latest"], "0x1000")
        
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST", 
            payload={
                "method": "eth_getBalance",
                "params": ["0x123", "latest"]
            }
        )
        
        # Handle the request
        result = await handler.handle_request(request)
        
        # Check that call was tracked
        assert len(handler.request_calls) == 1
        assert handler.request_calls[0] == request
        
        # Check result
        expected_json = json.dumps("0x1000").encode('utf-8')
        assert result == expected_json
    
    @pytest.mark.asyncio
    async def test_mock_request_handler_no_response(self):
        """Test handler behavior when no response is configured"""
        handler = MockRequestHandler()
        
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={
                "method": "unknown_method",
                "params": []
            }
        )
        
        # Should raise an error
        with pytest.raises(ColibriError) as exc_info:
            await handler.handle_request(request)
        
        assert "No mock response configured" in str(exc_info.value)
    
    @pytest.mark.asyncio
    async def test_mock_request_handler_default_response(self):
        """Test default response functionality"""
        handler = MockRequestHandler()
        
        default_data = b"default response data"
        handler.set_default_response(default_data)
        
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={
                "method": "unknown_method",
                "params": []
            }
        )
        
        result = await handler.handle_request(request)
        assert result == default_data
    
    def test_mock_request_handler_clear_operations(self):
        """Test clearing responses and calls"""
        handler = MockRequestHandler()
        
        # Add some data
        handler.add_response("method1", [], "response1")
        handler.add_method_response("method2", "response2")
        handler.set_default_response(b"default")
        
        # Simulate some calls
        handler.request_calls.append(DataRequest(1, "", "GET"))
        handler.request_calls.append(DataRequest(2, "", "POST"))
        
        # Clear responses
        handler.clear_responses()
        assert len(handler._responses) == 0
        assert len(handler._method_responses) == 0
        assert handler._default_response is None
        
        # Calls should still be there
        assert len(handler.request_calls) == 2
        
        # Clear calls
        handler.clear_calls()
        assert len(handler.request_calls) == 0
    
    def test_get_calls_for_method(self):
        """Test filtering calls by method"""
        handler = MockRequestHandler()
        
        # Add some requests with different methods
        request1 = DataRequest(1, "", "POST", payload={"method": "eth_getBalance"})
        request2 = DataRequest(2, "", "POST", payload={"method": "eth_getCode"})
        request3 = DataRequest(3, "", "POST", payload={"method": "eth_getBalance"})
        request4 = DataRequest(4, "", "GET")  # No payload
        
        handler.request_calls.extend([request1, request2, request3, request4])
        
        balance_calls = handler.get_calls_for_method("eth_getBalance")
        assert len(balance_calls) == 2
        assert request1 in balance_calls
        assert request3 in balance_calls
        
        code_calls = handler.get_calls_for_method("eth_getCode")
        assert len(code_calls) == 1
        assert request2 in code_calls
        
        unknown_calls = handler.get_calls_for_method("unknown_method")
        assert len(unknown_calls) == 0


class TestMockProofData:
    """Test mock proof data utilities"""
    
    def test_create_proof(self):
        """Test creating mock proof data"""
        method = "eth_getBalance"
        params = ["0x123", "latest"]
        result = "0x1000"
        
        proof = MockProofData.create_proof(method, params, result)
        
        # Should be valid JSON bytes
        assert isinstance(proof, bytes)
        
        # Should be parseable as JSON
        proof_dict = json.loads(proof.decode('utf-8'))
        assert proof_dict["method"] == method
        assert proof_dict["params"] == params
        assert proof_dict["result"] == result
        assert proof_dict["mock"] is True
    
    def test_create_empty_proof(self):
        """Test creating empty proof for LOCAL methods"""
        proof = MockProofData.create_empty_proof()
        assert proof == b""


class TestTestHelper:
    """Test the test helper utilities"""
    
    def test_setup_eth_get_balance_mock(self):
        """Test setting up eth_getBalance mock"""
        handler = MockRequestHandler()
        
        address = "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5"
        block = "latest"
        balance = "0x1bc16d674ec80000"  # 2 ETH
        
        TestHelper.setup_eth_get_balance_mock(handler, address, block, balance)
        
        # Verify the response was added
        key = handler._make_key("eth_getBalance", [address, block])
        assert key in handler._responses
        
        response = handler._responses[key]
        assert response["jsonrpc"] == "2.0"
        assert response["result"] == balance
    
    def test_setup_eth_get_block_mock(self):
        """Test setting up eth_getBlockByHash mock"""
        handler = MockRequestHandler()
        
        block_hash = "0x1234567890abcdef"
        block_data = {
            "number": "0x123",
            "hash": block_hash,
            "parentHash": "0xabcdef",
            "timestamp": "0x123456789"
        }
        
        TestHelper.setup_eth_get_block_mock(handler, block_hash, block_data)
        
        # Verify the response was added
        key = handler._make_key("eth_getBlockByHash", [block_hash, False])
        assert key in handler._responses
        
        response = handler._responses[key]
        assert response["result"] == block_data
    
    def test_setup_proof_mock(self):
        """Test setting up proof mock"""
        handler = MockRequestHandler()
        
        method = "eth_getBalance"
        params = ["0x123", "latest"]
        
        TestHelper.setup_proof_mock(handler, method, params)
        
        # Verify a proof response was added
        key = handler._make_key(method, params)
        assert key in handler._responses
        
        # Should be bytes
        response = handler._responses[key]
        assert isinstance(response, bytes)
    
    def test_setup_proof_mock_with_custom_proof(self):
        """Test setting up proof mock with custom proof data"""
        handler = MockRequestHandler()
        
        method = "eth_getBalance"
        params = ["0x123", "latest"]
        custom_proof = b"custom proof data"
        
        TestHelper.setup_proof_mock(handler, method, params, custom_proof)
        
        key = handler._make_key(method, params)
        assert handler._responses[key] == custom_proof
    
    def test_create_mock_storage_with_data(self):
        """Test creating mock storage with preset data"""
        preset_data = {
            "key1": b"value1",
            "key2": b"value2"
        }
        
        storage = TestHelper.create_mock_storage_with_data(preset_data)
        
        assert isinstance(storage, MockStorage)
        assert storage.get("key1") == b"value1"
        assert storage.get("key2") == b"value2"
        assert storage.get("key3") is None


class TestDataRequest:
    """Test DataRequest class"""
    
    def test_data_request_creation(self):
        """Test creating DataRequest objects"""
        request = DataRequest(
            req_ptr=12345,
            url="api/v1/test",
            method="POST",
            payload={"key": "value"},
            encoding="json",
            request_type="eth_rpc",
            exclude_mask=1,
            chain_id=1
        )
        
        assert request.req_ptr == 12345
        assert request.url == "api/v1/test"
        assert request.method == "POST"
        assert request.payload == {"key": "value"}
        assert request.encoding == "json"
        assert request.request_type == "eth_rpc"
        assert request.exclude_mask == 1
        assert request.chain_id == 1
    
    def test_data_request_from_dict(self):
        """Test creating DataRequest from dictionary"""
        data = {
            "req_ptr": 12345,
            "url": "api/v1/test",
            "method": "POST",
            "payload": {"key": "value"},
            "encoding": "json",
            "type": "beacon_api",
            "exclude_mask": "2",  # String should be converted
            "chain_id": 1
        }
        
        request = DataRequest.from_dict(data)
        
        assert request.req_ptr == 12345
        assert request.url == "api/v1/test"
        assert request.method == "POST"
        assert request.payload == {"key": "value"}
        assert request.encoding == "json"
        assert request.request_type == "beacon_api"
        assert request.exclude_mask == 2
        assert request.chain_id == 1
    
    def test_data_request_to_dict(self):
        """Test converting DataRequest to dictionary"""
        request = DataRequest(
            req_ptr=12345,
            url="api/v1/test",
            method="POST",
            payload={"key": "value"},
            encoding="ssz",
            request_type="beacon_api"
        )
        
        data = request.to_dict()
        
        assert data["req_ptr"] == 12345
        assert data["url"] == "api/v1/test"
        assert data["method"] == "POST"
        assert data["payload"] == {"key": "value"}
        assert data["encoding"] == "ssz"
        assert data["type"] == "beacon_api"
    
    def test_data_request_defaults(self):
        """Test DataRequest with default values"""
        request = DataRequest(req_ptr=12345, url="test", method="GET")
        
        assert request.payload is None
        assert request.encoding == "json"
        assert request.request_type == "eth_rpc"
        assert request.exclude_mask == 0
        assert request.chain_id == 1