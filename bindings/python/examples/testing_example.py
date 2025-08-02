#!/usr/bin/env python3
"""
Testing example for Colibri Python bindings

This example demonstrates how to:
1. Use mock storage and request handlers for testing
2. Set up test scenarios  
3. Verify client behavior without network calls
4. Test different error conditions
"""

import asyncio
import json
from colibri import Colibri, MethodType, ColibriError
from colibri.testing import MockStorage, MockRequestHandler, TestHelper, MockProofData


async def test_mock_storage():
    """Demonstrate mock storage functionality"""
    print("📦 Testing Mock Storage")
    print("-" * 30)
    
    # Create mock storage
    storage = MockStorage()
    
    # Preset some data
    storage.preset_data({
        "state_123": b"initial_state_data",
        "cached_proof": b"proof_bytes_here"
    })
    
    # Create client with mock storage
    client = Colibri(storage=storage)
    
    # Use the storage
    retrieved = client.storage.get("state_123")
    print(f"Retrieved from storage: {retrieved}")
    
    client.storage.set("new_key", b"new_value")
    print(f"Set calls tracked: {storage.set_calls}")
    print(f"Get calls tracked: {storage.get_calls}")
    
    print("✅ Mock storage test completed\n")


async def test_mock_request_handler():
    """Demonstrate mock request handler functionality"""
    print("🌐 Testing Mock Request Handler") 
    print("-" * 35)
    
    # Create mock request handler
    handler = MockRequestHandler()
    
    # Set up mock responses
    TestHelper.setup_eth_get_balance_mock(
        handler,
        address="0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5",
        block="latest", 
        balance="0x1bc16d674ec80000"  # 2 ETH
    )
    
    # Add method-level response for gas price
    handler.add_method_response("eth_gasPrice", {
        "jsonrpc": "2.0",
        "id": 1,
        "result": "0x4a817c800"  # 20 Gwei
    })
    
    # Create client with mock handler
    client = Colibri(request_handler=handler)
    
    # Simulate RPC calls (these would normally make network requests)
    print("Simulating RPC calls with mocks...")
    
    # Mock an unproofable method
    try:
        # We'll manually test the handler since we need the native module for full RPC
        from colibri.types import DataRequest
        
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={
                "method": "eth_gasPrice",
                "params": []
            }
        )
        
        response = await handler.handle_request(request)
        response_data = json.loads(response.decode('utf-8'))
        print(f"Gas price response: {response_data}")
        
        # Check call tracking
        print(f"Total requests handled: {len(handler.request_calls)}")
        gas_price_calls = handler.get_calls_for_method("eth_gasPrice")
        print(f"Gas price calls: {len(gas_price_calls)}")
        
    except Exception as e:
        print(f"Mock handler test (expected without native module): {e}")
    
    print("✅ Mock request handler test completed\n")


async def test_error_scenarios():
    """Test error handling scenarios"""
    print("⚠️  Testing Error Scenarios")
    print("-" * 30)
    
    # Test with unconfigured mock handler
    handler = MockRequestHandler()
    
    try:
        from colibri.types import DataRequest
        
        request = DataRequest(
            req_ptr=12345,
            url="",
            method="POST",
            payload={
                "method": "unconfigured_method",
                "params": []
            }
        )
        
        await handler.handle_request(request)
        
    except ColibriError as e:
        print(f"✅ Expected error caught: {e}")
    
    # Test default response
    handler.set_default_response(b"default_error_response")
    
    try:
        response = await handler.handle_request(request)
        print(f"✅ Default response: {response}")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
    
    print("✅ Error scenarios test completed\n")


async def test_proof_mocking():
    """Test proof mocking utilities"""
    print("🔐 Testing Proof Mocking")
    print("-" * 25)
    
    # Create mock proof data
    method = "eth_getBalance"
    params = ["0x123", "latest"]
    result = "0x1000"
    
    proof = MockProofData.create_proof(method, params, result)
    print(f"Mock proof created: {len(proof)} bytes")
    
    # Verify it's valid JSON
    proof_data = json.loads(proof.decode('utf-8'))
    print(f"Proof method: {proof_data['method']}")
    print(f"Proof result: {proof_data['result']}")
    print(f"Is mock: {proof_data['mock']}")
    
    # Test empty proof for LOCAL methods
    empty_proof = MockProofData.create_empty_proof()
    print(f"Empty proof: {empty_proof} (length: {len(empty_proof)})")
    
    print("✅ Proof mocking test completed\n")


async def test_integrated_scenario():
    """Test a complete integrated scenario"""
    print("🔗 Testing Integrated Scenario")
    print("-" * 32)
    
    # Set up complete test environment
    storage = MockStorage()
    handler = MockRequestHandler()
    
    # Preset storage data (simulating cached states)
    storage.preset_data({
        "sync_state_1": b"sync_committee_data",
        "block_state_123": b"block_header_data"
    })
    
    # Set up various mock responses
    TestHelper.setup_eth_get_balance_mock(
        handler, "0x123", "latest", "0x2000"
    )
    
    TestHelper.setup_eth_get_block_mock(
        handler, "0xabc123", {
            "number": "0x123456",
            "hash": "0xabc123",
            "timestamp": "0x64a7c123"
        }
    )
    
    # Create client
    client = Colibri(
        chain_id=1,
        storage=storage,
        request_handler=handler
    )
    
    print(f"Client configured with chain ID: {client.chain_id}")
    print(f"Storage has {storage.size()} preset items")
    print(f"Handler ready for mock responses")
    
    # Test method support
    balance_support = client.get_method_support("eth_getBalance")
    print(f"eth_getBalance support: {balance_support.name}")
    
    chainid_support = client.get_method_support("eth_chainId")
    print(f"eth_chainId support: {chainid_support.name}")
    
    # Simulate storage usage
    client.storage.set("test_state", b"new_test_data")
    retrieved = client.storage.get("test_state")
    print(f"Storage round-trip: {retrieved}")
    
    # Check what was tracked
    print(f"Storage operations tracked: {len(storage.get_calls + storage.set_calls)}")
    
    print("✅ Integrated scenario test completed\n")


async def main():
    """Run all test examples"""
    print("🧪 Colibri Python Bindings - Testing Examples")
    print("=" * 50)
    print("This example shows how to test Colibri without network calls\n")
    
    try:
        await test_mock_storage()
        await test_mock_request_handler()
        await test_error_scenarios()
        await test_proof_mocking()
        await test_integrated_scenario()
        
        print("🎉 All testing examples completed successfully!")
        print("\nTo run actual tests:")
        print("  python -m pytest tests/")
        print("  python -m pytest tests/test_storage.py -v")
        print("  python -m pytest tests/test_testing.py::TestMockRequestHandler -v")
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(main())