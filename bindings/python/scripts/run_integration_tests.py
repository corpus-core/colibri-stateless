#!/usr/bin/env python3
"""
Manual integration test runner for Colibri Python bindings
"""

import asyncio
import sys
from pathlib import Path

# Add src to path
current_dir = Path(__file__).parent
src_dir = current_dir.parent / 'src'
sys.path.insert(0, str(src_dir))

from colibri.testing import discover_tests, run_test_case


async def run_single_test():
    """Run a single test case for quick debugging"""
    
    print("🔧 SINGLE TEST DEBUG MODE")
    print("=" * 30)
    
    # Hard-coded test for quick debugging
    test_data_root = Path(__file__).parent.parent / '..' / '..' / 'test' / 'data'
    blocknum_dir = test_data_root / 'eth_blockNumber_electra'
    
    if not blocknum_dir.exists():
        print(f"❌ Test directory not found: {blocknum_dir}")
        return False
    
    # Create test case manually
    import json
    test_json_path = blocknum_dir / 'test.json'
    
    if not test_json_path.exists():
        print(f"❌ test.json not found: {test_json_path}")
        return False
    
    with open(test_json_path) as f:
        config = json.load(f)
    
    test_case = {
        'name': 'eth_blockNumber_electra',
        'directory': blocknum_dir,
        'method': config['method'],
        'params': config['params'],
        'chain_id': config['chain_id'],
        'expected_result': config.get('expected_result')
    }
    
    print(f"🧪 Running: {test_case['name']}")
    print(f"   Method: {test_case['method']}")
    print(f"   Params: {test_case['params']}")
    
    result = await run_test_case(test_case)
    
    if result['status'] == 'PASSED':
        print(f"✅ Test passed: {result['result']}")
        return True
    else:
        print(f"❌ Test failed: {result.get('error', 'Unknown error')}")
        return False


def main():
    success = asyncio.run(run_single_test())
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
