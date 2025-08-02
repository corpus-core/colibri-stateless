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
    """Run eth_blockNumber test case"""
    
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
    
    print(f"🧪 Running: eth_blockNumber")
    print(f"   Chain ID: {config['chain_id']}")
    print(f"   Expected: {config.get('expected_result')}")
    
    # Create mocks
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
    
    # Execute RPC call
    try:
        result = await client.rpc(config['method'], config['params'])
        
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
            
    except Exception as e:
        print(f"❌ Test error: {e}")
        return False


def main():
    success = asyncio.run(run_single_test())
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
