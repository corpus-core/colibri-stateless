"""
Tests for Colibri client functionality
"""

import pytest
from unittest.mock import Mock, patch
import json

from colibri import Colibri, MethodType, ColibriError, RPCError
from colibri.testing import MockStorage, MockRequestHandler, TestHelper
from colibri.storage import MemoryStorage


class TestColibriInit:
    """Test Colibri client initialization"""
    
    def test_init_with_defaults(self):
        """Test initialization with default values"""
        client = Colibri()
        
        assert client.chain_id == 1
        assert len(client.proofers) > 0
        assert len(client.eth_rpcs) > 0
        assert len(client.beacon_apis) > 0
        assert client.trusted_block_hashes == []
        assert client.include_code is False
        assert client.storage is not None
    
    def test_init_with_custom_values(self):
        """Test initialization with custom values"""
        storage = MemoryStorage()
        client = Colibri(
            chain_id=11155111,  # Sepolia
            proofers=["https://custom-proofer.com"],
            eth_rpcs=["https://custom-rpc.com"],
            beacon_apis=["https://custom-beacon.com"],
            trusted_block_hashes=["0x123abc"],
            include_code=True,
            storage=storage
        )
        
        assert client.chain_id == 11155111
        assert client.proofers == ["https://custom-proofer.com"]
        assert client.eth_rpcs == ["https://custom-rpc.com"]
        assert client.beacon_apis == ["https://custom-beacon.com"]
        assert client.trusted_block_hashes == ["0x123abc"]
        assert client.include_code is True
        assert client.storage is storage
    
    def test_default_configs_for_chains(self):
        """Test that default configurations exist for supported chains"""
        # Mainnet
        client1 = Colibri(chain_id=1)
        assert len(client1.proofers) > 0
        assert len(client1.eth_rpcs) > 0
        
        # Sepolia
        client2 = Colibri(chain_id=11155111)
        assert len(client2.proofers) > 0
        assert len(client2.eth_rpcs) > 0
        
        # Gnosis
        client3 = Colibri(chain_id=100)
        assert len(client3.proofers) > 0
        assert len(client3.eth_rpcs) > 0


class TestMethodSupport:
    """Test method support detection"""
    
    def test_get_method_support_proofable(self):
        """Test detection of proofable methods"""
        client = Colibri()
        
        proofable_methods = [
            "eth_getBalance",
            "eth_getCode",
            "eth_getStorageAt",
            "eth_getTransactionByHash",
            "eth_getTransactionReceipt",
            "eth_getBlockByHash",
            "eth_getBlockByNumber",
            "eth_getLogs",
            "eth_call",
            "eth_getProof"
        ]
        
        for method in proofable_methods:
            support = client.get_method_support(method)
            assert support == MethodType.PROOFABLE, f"Method {method} should be proofable"
    
    def test_get_method_support_local(self):
        """Test detection of local methods"""
        client = Colibri()
        
        local_methods = ["eth_chainId", "net_version"]
        
        for method in local_methods:
            support = client.get_method_support(method)
            assert support == MethodType.LOCAL, f"Method {method} should be local"
    
    def test_get_method_support_not_supported(self):
        """Test detection of unsupported methods"""
        client = Colibri()
        
        unsupported_methods = ["web3_sha3", "debug_traceTransaction"]
        
        for method in unsupported_methods:
            support = client.get_method_support(method)
            assert support == MethodType.NOT_SUPPORTED, f"Method {method} should not be supported"


class TestClientWithMocks:
    """Test client functionality with mocked dependencies"""
    
    @pytest.fixture
    def mock_client(self):
        """Create a client with mock storage and request handler"""
        storage = MockStorage()
        request_handler = MockRequestHandler()
        
        client = Colibri(
            chain_id=1,
            storage=storage,
            request_handler=request_handler
        )
        
        return client, storage, request_handler
    
    @pytest.mark.asyncio
    async def test_rpc_unproofable_method(self, mock_client):
        """Test RPC call for unproofable method"""
        client, storage, request_handler = mock_client
        
        # Mock the response for eth_gasPrice (unproofable)
        expected_result = "0x4a817c800"  # 20 Gwei
        request_handler.add_method_response("eth_gasPrice", {
            "jsonrpc": "2.0",
            "id": 1,
            "result": expected_result
        })
        
        # Mock the method support to return UNPROOFABLE
        with patch.object(client, 'get_method_support', return_value=MethodType.UNPROOFABLE):
            with patch.object(client, '_fetch_rpc', return_value=expected_result) as mock_fetch:
                result = await client.rpc("eth_gasPrice", [])
                
                assert result == expected_result
                mock_fetch.assert_called_once_with(client.eth_rpcs, "eth_gasPrice", [], as_proof=False)
    
    @pytest.mark.asyncio
    async def test_rpc_not_supported_method(self, mock_client):
        """Test RPC call for unsupported method"""
        client, storage, request_handler = mock_client
        
        with patch.object(client, 'get_method_support', return_value=MethodType.NOT_SUPPORTED):
            with pytest.raises(ColibriError) as exc_info:
                await client.rpc("unsupported_method", [])
            
            assert "not supported" in str(exc_info.value)
    
    @pytest.mark.asyncio
    async def test_storage_integration(self, mock_client):
        """Test that storage is properly integrated"""
        client, storage, request_handler = mock_client
        
        # Storage should be empty initially
        assert storage.size() == 0
        
        # Simulate some storage operations (these would happen during proof operations)
        test_key = "test_state_key"
        test_data = b"test_state_data"
        
        client.storage.set(test_key, test_data)
        assert client.storage.get(test_key) == test_data
        
        # Verify mock tracked the calls
        assert (test_key, test_data) in storage.set_calls
        assert test_key in storage.get_calls


class TestClientErrorHandling:
    """Test error handling in client"""
    
    def test_client_with_invalid_chain_id(self):
        """Test client behavior with unknown chain ID"""
        # Should not raise an error, just use defaults
        client = Colibri(chain_id=999999)
        assert client.chain_id == 999999
        assert len(client.proofers) > 0  # Should have fallback defaults
    
    @pytest.mark.asyncio
    async def test_rpc_with_empty_params(self):
        """Test RPC call with empty parameters"""
        client = Colibri()
        
        # Should not raise an error for empty params
        with patch.object(client, 'get_method_support', return_value=MethodType.LOCAL):
            with patch.object(client, 'verify_proof', return_value="0x1") as mock_verify:
                result = await client.rpc("eth_chainId", [])
                mock_verify.assert_called_once_with(b"", "eth_chainId", [])


class TestClientHelpers:
    """Test helper methods"""
    
    def test_default_server_configs(self):
        """Test that default server configurations are reasonable"""
        # Test that each supported chain has proper defaults
        for chain_id in [1, 11155111, 100, 10200]:
            proofers = Colibri._get_default_proofers(chain_id)
            eth_rpcs = Colibri._get_default_eth_rpcs(chain_id)
            beacon_apis = Colibri._get_default_beacon_apis(chain_id)
            
            assert len(proofers) > 0, f"Chain {chain_id} should have default proofers"
            assert len(eth_rpcs) > 0, f"Chain {chain_id} should have default ETH RPCs"
            assert len(beacon_apis) > 0, f"Chain {chain_id} should have default beacon APIs"
            
            # URLs should be valid HTTPS
            for url in proofers + eth_rpcs + beacon_apis:
                assert url.startswith("https://"), f"URL {url} should use HTTPS"
    
    def test_unknown_chain_defaults(self):
        """Test defaults for unknown chain"""
        unknown_chain_id = 999999
        
        proofers = Colibri._get_default_proofers(unknown_chain_id)
        eth_rpcs = Colibri._get_default_eth_rpcs(unknown_chain_id)
        beacon_apis = Colibri._get_default_beacon_apis(unknown_chain_id)
        
        # Should have fallback defaults
        assert len(proofers) > 0
        assert len(eth_rpcs) > 0
        assert len(beacon_apis) > 0