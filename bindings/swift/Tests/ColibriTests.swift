import XCTest
@testable import Colibri

final class ColibriTests: XCTestCase {
    func testProofManagerInitialization() {
        let proofManager = ColibriProofManager()
        XCTAssertNotNil(proofManager)
        
        // Test default values
        XCTAssertEqual(proofManager.chainId, 1)
        XCTAssertEqual(proofManager.trustedBlockHases, [])
    }
    
    func testCreateAndVerifyProof() async throws {
        let proofManager = ColibriProofManager()
        
        // Set up test values
        proofManager.eth_rpcs = ["https://mainnet.infura.io/v3/YOUR-PROJECT-ID"]
        proofManager.beacon_apis = ["https://beaconcha.in/api/v1"]
        
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
            let proof = try await proofManager.createProof(method: method, params: params)
            XCTAssertFalse(proof.isEmpty, "Proof should not be empty")
            
            // Verify proof
            let verificationResult = try await proofManager.verifyProof(proof: proof, method: method, params: params)
            XCTAssertFalse(verificationResult.isEmpty, "Verification result should not be empty")
            
            // Parse verification result
            if let jsonData = verificationResult.data(using: .utf8),
               let json = try? JSONSerialization.jsonObject(with: jsonData, options: []) as? [String: Any] {
                XCTAssertNotNil(json["result"], "Verification result should contain 'result' field")
            } else {
                XCTFail("Failed to parse verification result JSON")
            }
        } catch {
            XCTFail("Error during proof creation/verification: \(error)")
        }
    }
} 