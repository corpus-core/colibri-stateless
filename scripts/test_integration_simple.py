#!/usr/bin/env python3
"""
Simple integration test runner for Colibri Python bindings
"""

import asyncio
import json
import sys
from pathlib import Path

# Add src to Python path
current_dir = Path(__file__).parent
src_dir = current_dir.parent / 'src'
sys.path.insert(0, str(src_dir))

from colibri import Colibri
from colibri.testing import FileBasedMockStorage, FileBasedMockRequestHandler, discover_tests


async def run_single_test():
    """Run eth_blockNumber test case with improved error handling"""
    
    print("🔧 SIMPLE INTEGRATION TEST")
    print("=" * 30)
    
    # Find eth_blockNumber test case
    test_data_root = Path(__file__).parent.parent / '..' / '..' / 'test' / 'data'
    blocknum_dir = test_data_root / 'eth_blockNumber_electra'
    
    if not blocknum_dir.exists():
        print(f"❌ Test directory not found: {blocknum_dir}")
        return False
    
    # Load test configuration
    test_json_path = blocknum_dir / 'test.json'
    with open(test_json_path) as f:
        config = json.load(f)
    
    # Check if test should be skipped
    if config.get('requires_chain_store', False):
        print(f"⏭️ Skipping test - requires chain store")
        return True
    
    print(f"🧪 Running: eth_blockNumber")
    print(f"   Chain ID: {config['chain_id']}")
    print(f"   Expected: {config.get('expected_result')}")
    
    # Create mocks with loop prevention
    mock_storage = FileBasedMockStorage(blocknum_dir)
    mock_request_handler = FileBasedMockRequestHandler(blocknum_dir)
    
    # Create client
    client = Colibri(
        chain_id=config['chain_id'],
        storage=mock_storage,
        request_handler=mock_request_handler
    )
    
    # Test method support
    method_type = client.get_method_support(config['method'])
    print(f"🔍 Method support: {method_type.name}")
    
    # Execute RPC call with timeout to prevent hanging
    try:
        print(f"🚀 Executing RPC call...")
        result = await asyncio.wait_for(
            client.rpc(config['method'], config['params']),
            timeout=30.0  # 30 second timeout
        )
        
        print(f"✅ Test completed!")
        print(f"   Result: {result}")
        
        # Compare with expected
        if config.get('expected_result'):
            if result == config['expected_result']:
                print(f"✅ Matches expected result!")
                return True
            else:
                print(f"❌ Expected {config['expected_result']}, got {result}")
                return False
        else:
            print(f"ℹ️ No expected result to compare")
            return True
            
    except asyncio.TimeoutError:
        print(f"❌ Test timeout (30s) - possible infinite loop")
        return False
    except Exception as e:
        print(f"❌ Test error: {e}")
        import traceback
        print("Full traceback:")
        traceback.print_exc()
        return False


async def run_discovery_test():
    """Test the test discovery mechanism with chain store filtering"""
    
    print("🔍 TEST DISCOVERY WITH CHAIN STORE FILTERING")
    print("=" * 50)
    
    # Discover all tests
    test_cases = discover_tests()
    
    if not test_cases:
        print("❌ No test cases found!")
        return False
    
    print(f"\n📋 Found {len(test_cases)} executable test cases:")
    for i, test_case in enumerate(test_cases[:10], 1):  # Show first 10
        print(f"   {i:2d}. {test_case['name']:35} - {test_case['method']}")
    
    if len(test_cases) > 10:
        print(f"   ... and {len(test_cases) - 10} more")
    
    return True


async def run_quick_multiple_tests():
    """Run first few tests for broader validation"""
    
    print("⚡ QUICK MULTIPLE TESTS")
    print("=" * 30)
    
    test_cases = discover_tests()
    
    if not test_cases:
        print("❌ No test cases found!")
        return False
    
    # Run first 3 tests
    max_tests = min(3, len(test_cases))
    print(f"🧪 Running first {max_tests} tests...")
    
    passed = 0
    failed = 0
    
    for i, test_case in enumerate(test_cases[:max_tests], 1):
        print(f"\n--- Test {i}/{max_tests}: {test_case['name']} ---")
        
        try:
            mock_storage = FileBasedMockStorage(test_case['directory'])
            mock_request_handler = FileBasedMockRequestHandler(test_case['directory'])
            
            client = Colibri(
                chain_id=test_case['chain_id'],
                storage=mock_storage,
                request_handler=mock_request_handler
            )
            
            # Run with timeout
            result = await asyncio.wait_for(
                client.rpc(test_case['method'], test_case['params']),
                timeout=15.0
            )
            
            print(f"✅ PASSED: {result}")
            passed += 1
            
        except asyncio.TimeoutError:
            print(f"❌ TIMEOUT")
            failed += 1
        except Exception as e:
            print(f"❌ ERROR: {e}")
            failed += 1
    
    print(f"\n📊 Results: {passed} passed, {failed} failed")
    return failed == 0


def main():
    """Main entry point with interactive menu"""
    
    print("🎮 COLIBRI PYTHON INTEGRATION TESTS")
    print("=" * 40)
    print("1. Single test (eth_blockNumber)")
    print("2. Discovery test")
    print("3. Quick multiple tests")
    print("q. Quit")
    
    choice = input("\nYour choice: ").strip()
    
    if choice.lower() == 'q':
        print("👋 Goodbye!")
        return True
    elif choice == '1':
        success = asyncio.run(run_single_test())
    elif choice == '2':
        success = asyncio.run(run_discovery_test())
    elif choice == '3':
        success = asyncio.run(run_quick_multiple_tests())
    else:
        print("❌ Invalid choice")
        return False
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()