import XCTest
@testable import Colibri

final class ColibriTests: XCTestCase {
    func testColibriInitialization() {
        let colibri = Colibri()
        XCTAssertNotNil(colibri)
        
        // Test default values
        XCTAssertEqual(colibri.chainId, 1)
        XCTAssertEqual(colibri.trustedBlockHases, [])
    }
    
    func testCreateAndVerifyProof() async throws {
        let colibri = Colibri()
        
        // Set up test values
        colibri.eth_rpcs = ["https://mainnet.infura.io/v3/YOUR-PROJECT-ID"]
        colibri.beacon_apis = ["https://beaconcha.in/api/v1"]
        
        // Example eth_getBalance call
        let method = "eth_getBalance"
        let params = """
        {
            "address": "0x742d35Cc6634C0532925a3b844Bc454e4438f44e",
            "block": "latest"
        }
        """
        
        do {
            // Create proof
            let proof = try await colibri.createProof(method: method, params: params)
            XCTAssertFalse(proof.isEmpty, "Proof should not be empty")
            
            // Verify proof
            let verificationResult = try await colibri.verifyProof(proof: proof, method: method, params: params)
            XCTAssertNotNil(verificationResult, "Verification result should not be nil")
            
            // Check if result contains expected structure
            if let resultDict = verificationResult as? [String: Any] {
                XCTAssertNotNil(resultDict["result"], "Verification result should contain 'result' field")
            } else {
                // The result could be a direct value (string, number, etc.)
                // which is also valid for some RPC methods
                print("Verification result: \(verificationResult)")
            }
        } catch {
            XCTFail("Error during proof creation/verification: \(error)")
        }
    }
} 