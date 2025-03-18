import Foundation
// The C header is imported via -import-objc-header flag

public class Colibri {
    // Configuration with defaults
    public var eth_rpcs: [String] = ["https://mainnet.infura.io/v3/YOUR_API_KEY"]
    public var beacon_apis: [String] = ["https://beaconcha.in/api/v1"]
    public var trustedBlockHases: [String] = []
    public var chainId: UInt64 = 1 // Default: Ethereum Mainnet
    public var includeCode: Bool = false

    public init() {}

    public static func initialize() {
        // Placeholder for initialization if needed
    }


    // Create proof asynchronously
    public func createProof(method: String, params: String) async throws -> Data {
        // Create context
        let methodCStr = method.withCString { strdup(strPtr)
        }
        let paramsCStr = params.withCString { strdup(strPtr)
        }
        
        guard let methodPtr = methodCStr,
              let paramsPtr = paramsCStr else {
            throw ColibriError.invalidInput
        }
        
        defer {
            free(methodPtr)
            free(paramsCStr)
        }
        
        let ctx = create_proofer_ctx(methodPtr, paramsPtr, chainId, includeCode ? 1 : 0)
        defer { free_proofer_ctx(ctx) }

        while true {
            guard let jsonStatusPtr = proofer_execute_json_status(ctx) else {
                throw ColibriError.executionFailed
            }
            let jsonStatus = String(cString: jsonStatusPtr)
            free(jsonStatusPtr)

            let statusData = jsonStatus.data(using: String.Encoding.utf8)!
            guard let statusDict = try JSONSerialization.jsonObject(with: statusData) as? [String: Any],
                  let status = statusDict["status"] as? String else {
                throw ColibriError.invalidJSON
            }

            switch status {
            case "success":
                let proof = proofer_get_proof(ctx)
                // Create a new Data instance with copied bytes
                let proofData = Data(bytes: UnsafeRawPointer(proof.data), count: Int(proof.len))
                return proofData
            case "error":
                let errorMsg = statusDict["error"] as? String ?? "Unknown error"
                throw ColibriError.proofError(errorMsg)
            case "pending":
                guard let requests = statusDict["requests"] as? [[String: Any]] else {
                    throw ColibriError.invalidJSON
                }
                try await handleRequests(requests)
            default:
                throw ColibriError.unknownStatus(status)
            }
        }
    }

    // Verify proof asynchronously
    public func verifyProof(proof: Data, method: String, params: String) async throws -> Any {
        // Format trusted block hashes as JSON array string
        let trustedBlockHasesStr = if trustedBlockHases.isEmpty {
            "[]"
        } else {
            let quotedHashes = trustedBlockHases.map { "\"\($0)\"" }
            "[\(quotedHashes.joined(separator: ","))]"
        }
        
        let methodPtr = method.withCString { strdup($0) }
        let paramsPtr = params.withCString { strdup($0) }
        let trustedBlockHasesPtr = trustedBlockHasesStr.withCString { strdup($0) }
        
        guard let methodCStr = methodPtr,
              let paramsCStr = paramsPtr,
              let trustedBlockHasesCStr = trustedBlockHasesPtr else {
            throw ColibriError.invalidInput
        }
        
        defer {
            free(methodPtr)
            free(paramsPtr)
            free(trustedBlockHasesPtr)
        }
        
        // Create a copy of the proof data to ensure it remains valid
        let proofCopy = proof.copyBytes()
        var proofBytes = bytes_t()
        withUnsafeBytes(of: proofCopy) { rawBufferPointer in
            let bufferPointer = rawBufferPointer.bindMemory(to: UInt8.self)
            if let baseAddress = bufferPointer.baseAddress {
                let mutablePtr = UnsafeMutablePointer(mutating: baseAddress)
                proofBytes = bytes_t(data: mutablePtr, len: UInt32(proof.count))
            }
        }
        
        let ctx = verify_create_ctx(proofBytes, methodCStr, paramsCStr, chainId, trustedBlockHasesCStr)
        defer { verify_free_ctx(ctx) }

        while true {
            guard let jsonStatusPtr = verify_execute_json_status(ctx) else {
                throw ColibriError.executionFailed
            }
            let jsonStatus = String(cString: jsonStatusPtr)
            free(jsonStatusPtr)

            let statusData = jsonStatus.data(using: String.Encoding.utf8)!
            guard let statusDict = try JSONSerialization.jsonObject(with: statusData) as? [String: Any],
                  let status = statusDict["status"] as? String else {
                throw ColibriError.invalidJSON
            }

            switch status {
            case "success":
                guard let result = statusDict["result"] else {
                    throw ColibriError.invalidJSON
                }
                return result
            case "error":
                let errorMsg = statusDict["error"] as? String ?? "Unknown error"
                throw ColibriError.proofError(errorMsg)
            case "pending":
                guard let requests = statusDict["requests"] as? [[String: Any]] else {
                    throw ColibriError.invalidJSON
                }
                try await handleRequests(requests)
            default:
                throw ColibriError.unknownStatus(status)
            }
        }
    }

    // Helper function to handle pending requests
    private func handleRequests(_ requests: [[String: Any]]) async throws {
        await withTaskGroup(of: Void.self) { group in
            for request in requests {
                group.addTask {
                    guard let reqPtr = request["req_ptr"] as? UnsafeMutableRawPointer,
                          let uri = request["url"] as? String,
                          let method = request["method"] as? String,
                          let excludeMask = request["exclude_mask"] as? Int else {
                        return
                    }
                    
                    // Select appropriate server list based on request type
                    let servers = request["type"] as? String == "beacon" ? self.beacon_apis : self.eth_rpcs
                    var lastError = "No servers available"
                    
                    // Try each server in the list
                    for (index, server) in servers.enumerated() {
                        // Skip if server is excluded
                        if (excludeMask & (1 << index)) != 0 {
                            continue
                        }
                        
                        // Construct full URL
                        let fullUrl = uri.isEmpty ? server : "\(server)/\(uri)"
                        guard let url = URL(string: fullUrl) else {
                            continue
                        }
                        
                        do {
                            var request = URLRequest(url: url)
                            request.httpMethod = method
                            
                            // Set headers based on encoding type
                            if let encoding = request["encoding"] as? String {
                                if encoding == "json" {
                                    request.setValue("application/json", forHTTPHeaderField: "Accept")
                                } else {
                                    request.setValue("application/octet-stream", forHTTPHeaderField: "Accept")
                                }
                            }
                            
                            // Add payload if present
                            if let payload = request["payload"] as? [String: Any] {
                                request.setValue("application/json", forHTTPHeaderField: "Content-Type")
                                let payloadData = try JSONSerialization.data(withJSONObject: payload)
                                request.httpBody = payloadData
                            }
                            
                            let (responseData, response) = try await URLSession.shared.data(for: request)
                            
                            if let httpResponse = response as? HTTPURLResponse,
                               (200...299).contains(httpResponse.statusCode) {
                                // Success - set response and return
                                var bytes = bytes_t()
                                responseData.withUnsafeBytes { rawBufferPointer in
                                    let bufferPointer = rawBufferPointer.bindMemory(to: UInt8.self)
                                    if let baseAddress = bufferPointer.baseAddress {
                                        let mutablePtr = UnsafeMutablePointer(mutating: baseAddress)
                                        bytes = bytes_t(data: mutablePtr, len: UInt32(responseData.count))
                                    }
                                }
                                req_set_response(reqPtr, bytes, Int32(index))
                                return
                            } else {
                                lastError = "HTTP \((response as? HTTPURLResponse)?.statusCode ?? 0)"
                            }
                        } catch {
                            lastError = error.localizedDescription
                        }
                    }
                    
                    // If we get here, all servers failed
                    let errorPtr = lastError.withCString { strdup($0) }
                    if let errorCStr = errorPtr {
                        req_set_error(reqPtr, errorCStr, 0)
                        free(errorCStr)
                    }
                }
            }
        }
    }
    // Helper extension for safe byte copying
    private extension Data {
        func copyBytes() -> [UInt8] {
            return [UInt8](self)
        }
    }
}

// Error enum for better error handling
public enum ColibriError: Error {
    case invalidInput
    case executionFailed
    case invalidJSON
    case proofError(String)
    case unknownStatus(String)
    case invalidURL
}

