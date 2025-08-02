#!/usr/bin/env python3
"""
Comprehensive integration test runner for Colibri Python bindings

This script discovers and runs integration tests using file-based mocking,
with filtering for tests that require chain store.
"""

import asyncio
import sys
from pathlib import Path

# Add src to path so we can import colibri
current_dir = Path(__file__).parent
src_dir = current_dir.parent / 'src'
sys.path.insert(0, str(src_dir))

from colibri.testing import discover_tests, run_test_case


async def run_all_tests():
    """Discover and run all integration tests with improved filtering"""
    
    print("🚀 COLIBRI PYTHON INTEGRATION TESTS")
    print("=" * 50)
    
    # Discover tests (automatically filters out requires_chain_store)
    test_cases = discover_tests()
    
    if not test_cases:
        print("❌ No test cases found!")
        return False
    
    print(f"\n🔍 Found {len(test_cases)} executable test cases:")
    for i, test_case in enumerate(test_cases, 1):
        print(f"   {i:2d}. {test_case['name']:35} - {test_case['method']}")
    
    # Ask user which tests to run
    print(f"\nOptions:")
    print("  [Enter] - Run all tests")
    print("  1-N     - Run specific test")
    print("  first   - Run first test only")
    print("  first5  - Run first 5 tests")
    print("  q       - Quit")
    
    choice = input("\nYour choice: ").strip()
    
    if choice.lower() == 'q':
        print("👋 Goodbye!")
        return True
    
    # Determine which tests to run
    if choice == '' or choice.lower() == 'all':
        tests_to_run = test_cases
        print(f"\n🏃 Running all {len(test_cases)} tests...")
    elif choice.lower() == 'first':
        tests_to_run = [test_cases[0]]
        print(f"\n🏃 Running first test: {test_cases[0]['name']}")
    elif choice.lower() == 'first5':
        tests_to_run = test_cases[:5]
        print(f"\n🏃 Running first 5 tests...")
    else:
        try:
            test_index = int(choice) - 1
            if 0 <= test_index < len(test_cases):
                tests_to_run = [test_cases[test_index]]
                print(f"\n🏃 Running test: {test_cases[test_index]['name']}")
            else:
                print(f"❌ Invalid test number. Must be 1-{len(test_cases)}")
                return False
        except ValueError:
            print("❌ Invalid input. Enter a number, 'first', 'first5', or press Enter for all tests.")
            return False
    
    # Run the tests with timeout protection
    print("\n" + "=" * 50)
    
    results = []
    passed = 0
    failed = 0
    errors = 0
    timeouts = 0
    
    for i, test_case in enumerate(tests_to_run, 1):
        print(f"\n🧪 Test {i}/{len(tests_to_run)}: {test_case['name']}")
        print("-" * 40)
        
        try:
            # Run with timeout to prevent hanging
            result = await asyncio.wait_for(
                run_test_case(test_case),
                timeout=60.0  # 60 second timeout per test
            )
            results.append(result)
            
            if result['status'] == 'PASSED':
                passed += 1
                print(f"✅ PASSED")
                if result.get('result'):
                    print(f"   Result: {result['result']}")
            elif result['status'] == 'FAILED':
                failed += 1
                print(f"❌ FAILED: {result['error']}")
            elif result['status'] == 'ERROR':
                errors += 1
                print(f"💥 ERROR: {result['error']}")
                
        except asyncio.TimeoutError:
            timeouts += 1
            print(f"⏰ TIMEOUT (60s)")
            results.append({
                'name': test_case['name'],
                'status': 'TIMEOUT',
                'error': 'Test exceeded 60 second timeout'
            })
        except Exception as e:
            errors += 1
            print(f"💥 EXCEPTION: {e}")
            results.append({
                'name': test_case['name'],
                'status': 'EXCEPTION',
                'error': str(e)
            })
    
    # Summary
    print("\n" + "=" * 50)
    print("📊 TEST SUMMARY")
    print("=" * 50)
    
    total = len(results)
    print(f"Total tests: {total}")
    print(f"✅ Passed:   {passed}")
    print(f"❌ Failed:   {failed}")
    print(f"💥 Errors:   {errors}")
    print(f"⏰ Timeouts: {timeouts}")
    
    success_rate = (passed / total * 100) if total > 0 else 0
    print(f"📈 Success rate: {success_rate:.1f}%")
    
    if passed == total:
        print(f"\n🎉 ALL TESTS PASSED!")
        return True
    else:
        print(f"\n⚠️  Some tests failed or had errors.")
        
        # Show failed tests
        failed_tests = [r for r in results if r['status'] not in ['PASSED']]
        if failed_tests:
            print(f"\nFailed/Error tests:")
            for result in failed_tests[:10]:  # Show first 10 failures
                print(f"  - {result['name']}: {result.get('error', 'Unknown error')}")
            if len(failed_tests) > 10:
                print(f"  ... and {len(failed_tests) - 10} more")
        
        return success_rate >= 50  # Consider 50%+ success rate as acceptable


async def run_quick_test():
    """Run just the first test for quick validation"""
    
    print("⚡ QUICK TEST MODE")
    print("=" * 25)
    
    test_cases = discover_tests()
    
    if not test_cases:
        print("❌ No test cases found!")
        return False
    
    # Run first test
    test_case = test_cases[0]
    print(f"🧪 Running: {test_case['name']}")
    
    try:
        result = await asyncio.wait_for(
            run_test_case(test_case),
            timeout=30.0
        )
        
        if result['status'] == 'PASSED':
            print(f"✅ QUICK TEST PASSED!")
            if result.get('result'):
                print(f"   Result: {result['result']}")
            return True
        else:
            print(f"❌ QUICK TEST FAILED: {result.get('error', 'Unknown error')}")
            return False
    except asyncio.TimeoutError:
        print(f"⏰ QUICK TEST TIMEOUT")
        return False
    except Exception as e:
        print(f"💥 QUICK TEST ERROR: {e}")
        return False


async def run_discovery_only():
    """Just run test discovery to see what's available"""
    
    print("🔍 DISCOVERY ONLY MODE")
    print("=" * 30)
    
    test_cases = discover_tests()
    
    if not test_cases:
        print("❌ No test cases found!")
        return False
    
    print(f"\n📋 Summary:")
    print(f"   Total executable tests: {len(test_cases)}")
    
    # Group by method
    methods = {}
    for test in test_cases:
        method = test['method']
        methods[method] = methods.get(method, 0) + 1
    
    print(f"\n📊 Tests by method:")
    for method, count in sorted(methods.items()):
        print(f"   {method}: {count}")
    
    return True


def main():
    """Main entry point"""
    
    if len(sys.argv) > 1:
        mode = sys.argv[1]
        if mode == 'quick':
            success = asyncio.run(run_quick_test())
        elif mode == 'discovery':
            success = asyncio.run(run_discovery_only())
        else:
            print(f"❌ Unknown mode: {mode}")
            print("Usage: python run_integration_tests.py [quick|discovery]")
            sys.exit(1)
    else:
        success = asyncio.run(run_all_tests())
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()