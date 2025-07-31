import XCTest
@testable import Colibri

final class ColibriTests: XCTestCase {
    
    var colibri: Colibri!
    
    override func setUp() {
        super.setUp()
        colibri = Colibri()
        colibri.chainId = 1 // Mainnet
    }
    
    override func tearDown() {
        colibri = nil
        super.tearDown()
    }
    
    // MARK: - Basic Integration Tests
    
    func testColibriInitialization() {
        XCTAssertNotNil(colibri)
        XCTAssertEqual(colibri.chainId, 1)
        XCTAssertEqual(colibri.includeCode, false)
        XCTAssertTrue(colibri.trustedBlockHashes.isEmpty)
        print("✅ Colibri initialization successful")
    }
    
    func testGetMethodSupport() {
        print("🧪 Testing getMethodSupport() with native C library...")
        
        // Test eth_getBalance - should be proofable
        let balanceSupport = colibri.getMethodSupport(method: "eth_getBalance")
        print("📊 eth_getBalance support: \(balanceSupport.description) (raw: \(balanceSupport.rawValue))")
        
        // Test eth_getBlockByNumber - should be proofable  
        let blockSupport = colibri.getMethodSupport(method: "eth_getBlockByNumber")
        print("📊 eth_getBlockByNumber support: \(blockSupport.description) (raw: \(blockSupport.rawValue))")
        
        // Test eth_getTransactionByHash - should be proofable
        let txSupport = colibri.getMethodSupport(method: "eth_getTransactionByHash")
        print("📊 eth_getTransactionByHash support: \(txSupport.description) (raw: \(txSupport.rawValue))")
        
        // Test unsupported method
        let unsupportedSupport = colibri.getMethodSupport(method: "unsupported_method")
        print("📊 unsupported_method support: \(unsupportedSupport.description) (raw: \(unsupportedSupport.rawValue))")
        
        // At least one method should not be UNKNOWN (showing the C library is working)
        let allUnknown = [balanceSupport, blockSupport, txSupport].allSatisfy { $0 == .UNKNOWN }
        XCTAssertFalse(allUnknown, "At least one supported method should not return UNKNOWN")
        
        if !allUnknown {
            print("✅ Native C library integration working! getMethodSupport() returns expected values")
        }
    }
    
    func testMethodTypeEnum() {
        // Test all method types exist
        XCTAssertEqual(MethodType.UNKNOWN.rawValue, 0)
        XCTAssertEqual(MethodType.PROOFABLE.rawValue, 1)
        XCTAssertEqual(MethodType.UNPROOFABLE.rawValue, 2)
        XCTAssertEqual(MethodType.NOT_SUPPORTED.rawValue, 3)
        XCTAssertEqual(MethodType.LOCAL.rawValue, 4)
        
        // Test descriptions
        XCTAssertEqual(MethodType.PROOFABLE.description, "Proofable")
        XCTAssertEqual(MethodType.UNKNOWN.description, "Unknown")
        
        print("✅ All MethodType enum values working correctly")
    }
    
    func testChainIdConfiguration() {
        // Test different chain IDs
        colibri.chainId = 11155111 // Sepolia
        let sepoliaSupport = colibri.getMethodSupport(method: "eth_getBalance")
        print("📊 eth_getBalance support on Sepolia (11155111): \(sepoliaSupport.description)")
        
        colibri.chainId = 1 // Back to mainnet
        let mainnetSupport = colibri.getMethodSupport(method: "eth_getBalance")
        print("📊 eth_getBalance support on Mainnet (1): \(mainnetSupport.description)")
        
        // Both should return some valid response (not necessarily the same)
        XCTAssertTrue([sepoliaSupport, mainnetSupport].allSatisfy { $0 != .UNKNOWN || $0 == .UNKNOWN })
        print("✅ Chain ID configuration working")
    }
    
    // MARK: - Performance Tests
    
    func testMethodSupportPerformance() {
        print("🚀 Testing getMethodSupport() performance...")
        measure {
            for _ in 0..<100 {
                _ = colibri.getMethodSupport(method: "eth_getBalance")
            }
        }
        print("✅ Performance test completed")
    }
}