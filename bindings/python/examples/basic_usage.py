#!/usr/bin/env python3
"""
Basic usage example for Colibri Python bindings

This example demonstrates how to:
1. Initialize a Colibri client
2. Check method support
3. Execute RPC calls with proof generation and verification
4. Handle different method types
"""

import asyncio
import json
from colibri import Colibri, MethodType, ColibriError
from colibri.storage import DefaultStorage, MemoryStorage


async def main():
    print("🔐 Colibri Python Bindings - Basic Usage Example")
    print("=" * 50)
    
    # Example 1: Initialize client with default settings (Ethereum Mainnet)
    print("\n1. Initializing Colibri client...")
    client = Colibri(
        chain_id=1,  # Ethereum Mainnet
        storage=MemoryStorage()  # Use in-memory storage for this example
    )
    print(f"   Chain ID: {client.chain_id}")
    print(f"   Provers: {client.provers}")
    print(f"   ETH RPCs: {client.eth_rpcs}")
    print(f"   Beacon APIs: {client.beacon_apis}")
    
    # Example 2: Check method support
    print("\n2. Checking method support...")
    methods_to_check = [
        "eth_getBalance",
        "eth_getCode", 
        "eth_getStorageAt",
        "eth_chainId",
        "eth_gasPrice",
        "debug_traceTransaction"
    ]
    
    for method in methods_to_check:
        support = client.get_method_support(method)
        print(f"   {method:<25} -> {support.name:<15} ({support.description})")
    
    # Example 3: Execute a LOCAL method (no proof needed)
    print("\n3. Executing local method (eth_chainId)...")
    try:
        chain_id = await client.rpc("eth_chainId", [])
        print(f"   Chain ID: {chain_id}")
    except ColibriError as e:
        print(f"   Error: {e}")
        print("   Note: This requires the native module to be built")
    
    # Example 4: Demonstrate different client configurations
    print("\n4. Different client configurations...")
    
    # Sepolia testnet
    sepolia_client = Colibri(
        chain_id=11155111,
        storage=MemoryStorage()
    )
    print(f"   Sepolia client - Chain ID: {sepolia_client.chain_id}")
    
    # Gnosis Chain
    gnosis_client = Colibri(
        chain_id=100,
        storage=MemoryStorage()
    )
    print(f"   Gnosis client - Chain ID: {gnosis_client.chain_id}")
    
    # Custom configuration
    custom_client = Colibri(
        chain_id=1,
        eth_rpcs=["https://rpc.ankr.com/eth", "https://cloudflare-eth.com"],
        beacon_apis=["https://lodestar-mainnet.chainsafe.io"],
        trusted_checkpoint=None,
        include_code=True,
        storage=DefaultStorage()  # Use file-based storage
    )
    print(f"   Custom client - Include code: {custom_client.include_code}")
    
    # Example 5: Mock a proofable method call (without actually calling network)
    print("\n5. Example of how a proofable method would work...")
    print("   Method: eth_getBalance")
    print("   Address: 0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5")
    print("   Block: latest")
    print("   -> This would:")
    print("      1. Create a proof for the balance query")
    print("      2. Verify the proof cryptographically") 
    print("      3. Return the verified balance")
    print("      (Requires network access and native module)")
    
    # Example 6: Storage demonstration
    print("\n6. Storage demonstration...")
    storage = MemoryStorage()
    
    # Store some data
    storage.set("test_key", b"test_value")
    storage.set("proof_cache_123", b"cached_proof_data")
    
    print(f"   Stored items: {storage.size()}")
    print(f"   Retrieved: {storage.get('test_key')}")
    
    # Clear storage
    storage.clear()
    print(f"   After clear: {storage.size()}")
    
    print("\n✅ Example completed!")
    print("\nNext steps:")
    print("- Build the native module: ./build.sh")
    print("- Run tests: python -m pytest tests/")
    print("- Try real RPC calls with network access")


if __name__ == "__main__":
    asyncio.run(main())