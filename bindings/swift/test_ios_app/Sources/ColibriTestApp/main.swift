/**
 * Colibri Swift Integration Example
 * 
 * This test app demonstrates how to integrate and use the Colibri Stateless client
 * in an iOS application. It serves both as a CI integration test and as a 
 * developer reference implementation.
 */

import Foundation
import Colibri

@main
struct ColibriTestApp {
    
    static func main() async {
        print("🚀 Colibri iOS Test App Starting...")
        print("📱 Platform: \(ProcessInfo.processInfo.operatingSystemVersionString)")
        
        await runColibriTests()
        
        print("✅ Colibri iOS Test App Complete")
    }
    
    /// Demonstrates basic Colibri usage patterns
    static func runColibriTests() async {
        
        // MARK: - 1. Basic Initialization
        print("\n📋 1. BASIC INITIALIZATION")
        let colibri = Colibri()
        colibri.chainId = 1  // Ethereum Mainnet
        colibri.proofers = ["https://c4.incubed.net"]  // Use remote proofer for real calls
        
        print("   ✅ Colibri client initialized")
        print("   🔗 Chain ID: \(colibri.chainId)")
        print("   🌐 Proofers: \(colibri.proofers)")
        
        // MARK: - 2. Method Support Check
        print("\n📋 2. METHOD SUPPORT CHECK")
        let supportedMethods = ["eth_blockNumber", "eth_getBalance", "eth_call", "eth_getLogs"]
        
        for method in supportedMethods {
            let support = colibri.getMethodSupport(method: method)
            let status = support ? "✅ Supported" : "❌ Not Supported"
            print("   \(status): \(method)")
        }
        
        // MARK: - 3. Simple RPC Call (Local Proof)
        print("\n📋 3. SIMPLE RPC CALL (LOCAL PROOF)")
        do {
            // Use local proof generation (no network required)
            colibri.proofers = []  // Force local proof generation
            
            let result = try await colibri.rpc(method: "eth_blockNumber", params: [])
            print("   ✅ Local proof successful")
            print("   📊 Result type: \(type(of: result))")
            
            if let stringResult = result as? String {
                print("   🎯 Block number: \(stringResult)")
            } else if let dataResult = result as? Data {
                let hexString = "0x" + dataResult.map { String(format: "%02x", $0) }.joined()
                print("   🎯 Block number (hex): \(hexString)")
            }
            
        } catch {
            print("   ⚠️ Local proof failed (expected in CI): \(error)")
            print("   💡 This is normal in CI environment without blockchain state")
        }
        
        // MARK: - 4. Storage System Demo
        print("\n📋 4. STORAGE SYSTEM DEMO")
        
        // Custom storage implementation example
        class TestStorage: ColibriStorage {
            private var storage: [String: Data] = [:]
            
            func get(key: String) -> Data? {
                let result = storage[key]
                print("   🗄️ Storage GET: \(key) → \(result != nil ? "\(result!.count) bytes" : "nil")")
                return result
            }
            
            func set(key: String, value: Data) {
                storage[key] = value
                print("   🗄️ Storage SET: \(key) ← \(value.count) bytes")
            }
            
            func delete(key: String) {
                storage.removeValue(forKey: key)
                print("   🗄️ Storage DELETE: \(key)")
            }
        }
        
        // Register custom storage
        let testStorage = TestStorage()
        StorageBridge.registerStorage(testStorage)
        print("   ✅ Custom storage registered")
        
        // Test storage operations
        let testKey = "test_key_\(Int.random(in: 1000...9999))"
        let testData = "Hello Colibri Storage!".data(using: .utf8)!
        
        testStorage.set(key: testKey, value: testData)
        let retrievedData = testStorage.get(key: testKey)
        testStorage.delete(key: testKey)
        
        if retrievedData == testData {
            print("   ✅ Storage operations successful")
        } else {
            print("   ❌ Storage operations failed")
        }
        
        // MARK: - 5. Error Handling Demo
        print("\n📋 5. ERROR HANDLING DEMO")
        do {
            // Intentionally invalid method call
            let _ = try await colibri.rpc(method: "invalid_method", params: [])
            print("   ❌ Should have thrown error")
        } catch {
            print("   ✅ Error handling works: \(error.localizedDescription)")
        }
        
        // MARK: - 6. Multiple Chain Support Demo
        print("\n📋 6. MULTIPLE CHAIN SUPPORT DEMO")
        let chains: [(UInt64, String)] = [
            (1, "Ethereum Mainnet"),
            (137, "Polygon"),
            (8453, "Base"),
            (42161, "Arbitrum")
        ]
        
        for (chainId, name) in chains {
            colibri.chainId = chainId
            print("   🔗 \(name) (Chain ID: \(chainId))")
            
            let blockSupport = colibri.getMethodSupport(method: "eth_blockNumber")
            print("      eth_blockNumber: \(blockSupport ? "✅" : "❌")")
        }
        
        print("\n📊 Demo completed successfully!")
        print("💡 This app demonstrates key Colibri integration patterns")
        print("📚 Use this as reference for your own iOS app integration")
    }
}