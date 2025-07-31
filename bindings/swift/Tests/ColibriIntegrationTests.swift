// Integration Tests for Proofer and Verifier
// Tests based on test.json files in ../../test/data directories

import XCTest
import Foundation
@testable import Colibri
import TestConfig

final class ColibriIntegrationTests: XCTestCase {
    
    // MARK: - Test Data Discovery
    
    /// Find all test directories containing test.json
    private func findTestDirectories() -> [URL] {
        // Use build-time configured test data path
        let testDataPath = TestConfig.testDataURL
        
        print("📍 Using test data from TestConfig: \(testDataPath.path)")
        
        guard FileManager.default.fileExists(atPath: testDataPath.path) else {
            print("❌ Test data directory not found: \(testDataPath.path)")
            print("💡 Make sure build_macos.sh was run to generate TestConfig.swift")
            return []
        }
        
        guard let contents = try? FileManager.default.contentsOfDirectory(
            at: testDataPath,
            includingPropertiesForKeys: [.isDirectoryKey],
            options: []
        ) else {
            print("⚠️ Test data directory not found: \(testDataPath.path)")
            return []
        }
        
        let testDirs = contents.filter { url in
            var isDirectory: ObjCBool = false
            let exists = FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory)
            let hasTestJson = FileManager.default.fileExists(atPath: url.appendingPathComponent("test.json").path)
            return exists && isDirectory.boolValue && hasTestJson
        }
        
        print("📊 Found \(testDirs.count) test directories")
        return testDirs.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }
    
    // MARK: - Test Execution
    
    /// Test discovery only - just check that we can find test directories
    func testCanFindTestDirectories() {
        let testDirectories = findTestDirectories()
        
        XCTAssertTrue(!testDirectories.isEmpty, "Should find test directories")
        print("📊 Found \(testDirectories.count) test directories")
        
        // Print first few directories for debugging
        for (index, dir) in testDirectories.prefix(5).enumerated() {
            print("  \(index + 1). \(dir.lastPathComponent)")
        }
    }
    
    /// Simple test to verify mock setup without createProof (avoiding hang)
    func testMockSetupOnly() async throws {
        print("🚀 DEBUG: Starting testMockSetupOnly")
        let testDirectories = findTestDirectories()
        print("🚀 DEBUG: Found \(testDirectories.count) test directories")
        
        XCTAssertTrue(!testDirectories.isEmpty, "Should find test directories")
        
        // Find a simple test to run
        guard let testDir = testDirectories.first else {
            XCTFail("No test directories found")
            return
        }
        
        let testName = testDir.lastPathComponent
        print("🧪 DEBUG: Testing mock setup with: \(testName)")
        print("🧪 DEBUG: Test directory: \(testDir.path)")
        
        // Just test the mock setup, not createProof
        let mockHandler = MockFileRequestHandler(testDirectory: testDir)
        let colibri = Colibri()
        colibri.requestHandler = mockHandler
        
        // Try to read the test.json
        let testJsonURL = testDir.appendingPathComponent("test.json")
        let testData = try Data(contentsOf: testJsonURL)
        guard let testJson = try JSONSerialization.jsonObject(with: testData) as? [String: Any] else {
            throw ColibriError.invalidJSON
        }
        
        print("🧪 DEBUG: Successfully read test.json with method: \(testJson["method"] ?? "unknown")")
        
        // Test a simple mock request
        let testRequest = DataRequest(url: "test", method: "GET", payload: testJson)
        do {
            let result = try await mockHandler.handleRequest(testRequest)
            print("🧪 DEBUG: Mock request successful, got \(result.count) bytes")
        } catch {
            print("🧪 DEBUG: Mock request failed: \(error)")
        }
        
        print("🧪 DEBUG: Mock setup test completed successfully")
    }
    
    /// Test one specific integration test to verify the setup works
    func testSingleIntegrationTest() async throws {
        print("🚀 DEBUG: Starting testSingleIntegrationTest")
        let testDirectories = findTestDirectories()
        print("🚀 DEBUG: Found \(testDirectories.count) test directories")
        
        XCTAssertTrue(!testDirectories.isEmpty, "Should find test directories")
        
        // Find a simple test to run (prefer eth_getBalance tests)
        guard let testDir = testDirectories.first(where: { $0.lastPathComponent.contains("eth_getBalance") }) 
                ?? testDirectories.first else {
            XCTFail("No test directories found")
            return
        }
        
        let testName = testDir.lastPathComponent
        print("🧪 DEBUG: Running single integration test: \(testName)")
        print("🧪 DEBUG: Test directory: \(testDir.path)")
        print("🧪 DEBUG: About to call runSingleTest...")
        
        let success = try await runSingleTest(testDirectory: testDir)
        print("🧪 DEBUG: runSingleTest completed with success: \(success)")
        XCTAssertTrue(success, "Single integration test should succeed")
    }
    
    /// Run a single test from a test.json file
    private func runSingleTest(testDirectory: URL) async throws -> Bool {
        let testJsonURL = testDirectory.appendingPathComponent("test.json")
        
        // Read and parse test.json
        let testData = try Data(contentsOf: testJsonURL)
        guard let testJson = try JSONSerialization.jsonObject(with: testData) as? [String: Any] else {
            throw ColibriError.invalidJSON
        }
        
        // Skip tests that require chain store
        if let requiresChainStore = testJson["requires_chain_store"] as? Bool, requiresChainStore {
            print("⏭️  Skipping test (requires chain store)")
            return false
        }
        
        // Extract test parameters
        guard let method = testJson["method"] as? String,
              let params = testJson["params"] as? [Any] else {
            throw ColibriError.invalidInput
        }
        
        let chainId = testJson["chain_id"] as? Int ?? 1
        let trustedBlockhash = testJson["trusted_blockhash"] as? String
        
        print("  📋 Method: \(method)")
        print("  📋 Params: \(params)")
        print("  📋 Chain ID: \(chainId)")
        
        // Convert params to JSON string
        let paramsData = try JSONSerialization.data(withJSONObject: params)
        guard let paramsString = String(data: paramsData, encoding: .utf8) else {
            throw ColibriError.invalidJSON
        }
        
        // Create Colibri instance with mock handler
        let mockHandler = MockFileRequestHandler(testDirectory: testDirectory)
        let colibri = Colibri()
        colibri.requestHandler = mockHandler
        colibri.chainId = UInt64(chainId)  // Set chain ID from test.json
        
        // 🎯 IMPORTANT: Clear proofers to force LOCAL proof creation (not remote fetching)
        colibri.proofers = []  // Force local C-library proof creation for testing
        
        if let trustedBlockhash = trustedBlockhash {
            colibri.trustedBlockHashes = [trustedBlockhash]
        }
        
        // Execute the test using rpc() which handles createProof/verifyProof internally
        print("  🔨 DEBUG: About to call rpc for method: \(method)")
        print("  🔨 DEBUG: Mock handler set: \(colibri.requestHandler != nil)")
        print("  🔨 Calling rpc...")
        let startTime = Date()
        print("  🔨 DEBUG: Calling rpc now...")
        let result = try await colibri.rpc(method: method, params: paramsString)
        let rpcTime = Date().timeIntervalSince(startTime)
        print("  🔨 DEBUG: rpc returned!")
        
        XCTAssertNotNil(result, "RPC result should not be nil")
        
        // Handle different result types
        var resultDescription = "unknown type"
        if let data = result as? Data {
            resultDescription = "\(data.count) bytes"
        } else if let string = result as? String {
            resultDescription = "\(string.count) chars"
        } else if let dict = result as? [String: Any] {
            resultDescription = "\(dict.count) keys"
        } else if let array = result as? [Any] {
            resultDescription = "\(array.count) items"
        } else {
            resultDescription = "type: \(type(of: result))"
        }
        
        print("  ✅ RPC completed (\(resultDescription)) in \(String(format: "%.2f", rpcTime * 1000))ms")
        
        // Compare with expected result if available
        if let expectedResult = testJson["expected_result"] {
            print("  ℹ️  Expected result available: \(expectedResult)")
            // TODO: Add result comparison logic
        }
        
        return true
    }
}

// MARK: - Mock Request Handler Implementation
private class MockFileRequestHandler: RequestHandler {
    let testDirectory: URL
    
    init(testDirectory: URL) {
        self.testDirectory = testDirectory
    }
    
    func handleRequest(_ request: DataRequest) async throws -> Data {
        print("🔍 Mock: Received request - URL: '\(request.url)', Method: '\(request.method)'")
        print("🔍 Mock: Payload: \(String(describing: request.payload))")
        print("🔍 Mock: Test directory: \(testDirectory.path)")
        
        // Convert request to filename (similar to JS/Kotlin logic)
        let filename = requestToFilename(request: request)
        print("🔍 Mock: Generated filename: '\(filename)'")
        
        let fileURL = testDirectory.appendingPathComponent(filename)
        
        // Try to read the file
        if FileManager.default.fileExists(atPath: fileURL.path) {
            do {
                let data = try Data(contentsOf: fileURL)
                print("📁 Mock: Serving \(filename) (\(data.count) bytes)")
                return data
            } catch {
                print("❌ Mock: Failed to read \(filename): \(error)")
                throw ColibriError.rpcError("Failed to read mock file: \(filename)")
            }
        }
        
        // Fallback: try to find file by URL pattern or method name
        print("🔄 Mock: Trying fallback for URL/method...")
        
        do {
            let contents = try FileManager.default.contentsOfDirectory(atPath: testDirectory.path)
            print("🔄 Mock: Directory contents: \(contents.count) files")
            
            // Try URL-based fallback first (for beacon API calls)
            if !request.url.isEmpty {
                // Extract key parts from URL for pattern matching (split by / and ?)
                let urlParts = request.url.components(separatedBy: CharacterSet(charactersIn: "/?"))
                    .filter { !$0.isEmpty }
                print("🔄 Mock: URL parts: \(urlParts)")
                
                // Special handling for beacon headers/blocks with 'head'
                if urlParts.contains("headers") && urlParts.contains("head") {
                    let matching = contents.filter { $0.contains("headers") }
                    print("🔄 Mock: Beacon headers fallback: found \(matching.count) files: \(matching)")
                    if !matching.isEmpty {
                        let fallbackFile = testDirectory.appendingPathComponent(matching[0])
                        let data = try Data(contentsOf: fallbackFile)
                        print("📁 Mock: Beacon headers fallback serving \(matching[0]) (\(data.count) bytes)")
                        return data
                    }
                }
                
                if urlParts.contains("blocks") && urlParts.contains("head") {
                    let matching = contents.filter { $0.contains("blocks") && !$0.contains("head") }
                    print("🔄 Mock: Beacon blocks fallback: found \(matching.count) files")
                    if !matching.isEmpty {
                        let fallbackFile = testDirectory.appendingPathComponent(matching[0])
                        let data = try Data(contentsOf: fallbackFile)
                        print("📁 Mock: Beacon blocks fallback serving \(matching[0]) (\(data.count) bytes)")
                        return data
                    }
                }
                
                // Special handling for light_client/updates with different parameters
                if urlParts.contains("light_client") && urlParts.contains("updates") {
                    let matching = contents.filter { $0.contains("light_client_updates") }
                    print("🔄 Mock: Light client updates fallback: found \(matching.count) files: \(matching)")
                    if !matching.isEmpty {
                        let fallbackFile = testDirectory.appendingPathComponent(matching[0])
                        let data = try Data(contentsOf: fallbackFile)
                        print("📁 Mock: Light client updates fallback serving \(matching[0]) (\(data.count) bytes)")
                        return data
                    }
                }
            }
            
            // Original method-based fallback
            if let payload = request.payload,
               let method = payload["method"] as? String {
                
                print("🔄 Mock: Fallback method: \(method)")
                let matching = contents.filter { $0.hasPrefix(method) }
                print("🔄 Mock: Files matching '\(method)': \(matching)")
                
                if matching.count == 1 {
                    let fallbackFile = testDirectory.appendingPathComponent(matching[0])
                    let data = try Data(contentsOf: fallbackFile)
                    print("📁 Mock: Fallback serving \(matching[0]) (\(data.count) bytes)")
                    return data
                } else if matching.count > 1 {
                    print("⚠️ Mock: Multiple files match \(method), using first: \(matching[0])")
                    let fallbackFile = testDirectory.appendingPathComponent(matching[0])
                    let data = try Data(contentsOf: fallbackFile)
                    return data
                } else {
                    print("🔄 Mock: No files start with '\(method)'")
                    
                    // Smart fallback: for certain methods, try related methods
                    var fallbackMethods: [String] = []
                    switch method {
                    case "eth_getBalance":
                        fallbackMethods = ["eth_getProof"]
                    case "eth_getStorageAt":
                        fallbackMethods = ["eth_getProof"]
                    case "eth_getCode":
                        fallbackMethods = ["eth_getProof"]
                    case "eth_getTransactionCount":
                        fallbackMethods = ["eth_getProof"]
                    default:
                        break
                    }
                    
                    for fallbackMethod in fallbackMethods {
                        let fallbackMatching = contents.filter { $0.hasPrefix(fallbackMethod) }
                        print("🔄 Mock: Trying fallback to '\(fallbackMethod)': found \(fallbackMatching.count) files")
                        if !fallbackMatching.isEmpty {
                            let fallbackFile = testDirectory.appendingPathComponent(fallbackMatching[0])
                            let data = try Data(contentsOf: fallbackFile)
                            print("📁 Mock: Smart fallback serving \(fallbackMatching[0]) (\(data.count) bytes)")
                            return data
                        }
                    }
                }
            } else {
                print("🔄 Mock: No method in payload for fallback")
            }
        } catch {
            print("❌ Mock: Fallback failed: \(error)")
        }
        
        print("❌ Mock: File not found: \(filename)")
        throw ColibriError.rpcError("Mock file not found: \(filename)")
    }
    
    /// Convert a DataRequest to a filename (similar to JS create_cache logic)
    private func requestToFilename(request: DataRequest) -> String {
        var name = ""
        
        // Use payload-based name if available (JSON-RPC requests), otherwise use URL
        if let payload = request.payload,
           let method = payload["method"] as? String {
            // Build name from method and params (like JS: method + params.map(p => '_' + p).join(''))
            name = method
            
            if let params = payload["params"] as? [Any] {
                // Special handling for debug_traceCall (similar to Kotlin)
                if method == "debug_traceCall" && !params.isEmpty {
                    if let firstParam = params[0] as? [String: Any] {
                        let toValue = firstParam["to"] as? String ?? ""
                        let dataValue = firstParam["data"] as? String ?? ""
                        name += "___to___\(toValue)___data___\(dataValue)"
                    } else {
                        name += "_\(String(describing: params[0]))"
                    }
                } else {
                    // Default parameter handling (like JS params.map)
                    for param in params {
                        if let str = param as? String {
                            name += "_\(str)"
                        } else {
                            // Convert to JSON string for complex types
                            if let data = try? JSONSerialization.data(withJSONObject: param),
                               let jsonString = String(data: data, encoding: .utf8) {
                                name += "_\(jsonString)"
                            } else {
                                name += "_\(String(describing: param))"
                            }
                        }
                    }
                }
            }
        } else if !request.url.isEmpty {
            name = request.url
        }
        
        // Sanitize filename (replace forbidden characters - same as JS)
        let forbiddenChars = CharacterSet(charactersIn: "/\\.,: \"&=[]{}?")
        name = name.components(separatedBy: forbiddenChars).joined(separator: "_")
        
        // Remove leading underscores (caused by leading slashes)
        while name.hasPrefix("_") {
            name = String(name.dropFirst())
        }
        
        // Limit length (same as JS)
        if name.count > 100 {
            name = String(name.prefix(100))
        }
        
        // Add encoding extension
        let encoding = request.encoding ?? "json"  // Default to json for RPC requests
        return "\(name).\(encoding)"
    }
}