import Foundation
// The C header is imported via -import-objc-header flag

// Add the MethodType enum
public enum MethodType: Int {
    case PROOFABLE = 1
    case UNPROOFABLE = 2
    case NOT_SUPPORTED = 3
    case LOCAL = 4
    case UNKNOWN = 0 // Add an unknown case for safety
}

public class Colibri {
    // Configuration with defaults
    public var eth_rpcs: [String] = []
    public var beacon_apis: [String] = []
    public var proofers: [String] = ["https://c4.incubed.net"]
    public var trustedBlockHases: [String] = []
    public var chainId: UInt64 = 1 // Default: Ethereum Mainnet
    public var includeCode: Bool = false

    public init() {}

    public static func initialize() {
        // Placeholder for initialization if needed
    }

    // Add the getMethodSupport function
    public func getMethodSupport(method: String) async throws -> MethodType {
        let methodPtr = method.withCString { strdup($0) }
        guard let methodCStr = methodPtr else {
            throw ColibriError.invalidInput
        }
        defer { free(methodCStr) }

        let typeRaw = c4_get_method_type(chainId, methodCStr)
        guard let type = MethodType(rawValue: Int(typeRaw)) else {
             // Handle cases where the C function might return an unexpected value
            print("Warning: Unknown method type raw value \(typeRaw) returned from c4_get_method_type for method \(method)")
            return .UNKNOWN // Or throw an error, depending on desired strictness
        }
        return type
    }

    // Create proof asynchronously
    public func createProof(method: String, params: String) async throws -> Data {
        // Create context with proper memory management
        let methodCStr = method.withCString { strdup($0) }
        let paramsCStr = params.withCString { strdup($0) }
        
        guard let methodPtr = methodCStr,
              let paramsPtr = paramsCStr else {
            throw ColibriError.invalidInput
        }
        
        defer {
            free(methodPtr)
            free(paramsPtr)
        }
        
        guard let ctx = c4_create_proofer_ctx(methodPtr, paramsPtr, chainId, includeCode ? 1 : 0) else {
            throw ColibriError.contextCreationFailed
        }
        defer { c4_free_proofer_ctx(ctx) }

        while true {
            guard let jsonStatusPtr = c4_proofer_execute_json_status(ctx) else {
                throw ColibriError.nullPointerReceived
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
                let proof = c4_proofer_get_proof(ctx)
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
                try await handleRequests(requests, useProoferFallback: false)
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
        
        // Create bytes_t struct for proof data with safe memory handling
        let proofBytes = proof.withUnsafeBytes { rawBufferPointer in
            bytes_t(
                data: UnsafeMutablePointer(mutating: rawBufferPointer.bindMemory(to: UInt8.self).baseAddress!),
                len: UInt32(proof.count)
            )
        }
        
        guard let ctx = c4_verify_create_ctx(proofBytes, methodCStr, paramsCStr, chainId, trustedBlockHasesCStr) else {
            throw ColibriError.contextCreationFailed
        }
        defer { c4_verify_free_ctx(ctx) }

        var iteration = 0
        let maxIterations = 10 // Define a default maxIterations
        while true {
            iteration += 1
//            print("verifyProof: Iteration \(iteration)/\(maxIterations)")
            guard let statusJsonPtr = c4_verify_execute_json_status(ctx) else {
                throw ColibriError.nullPointerReceived
            }
            let statusJsonString = String(cString: statusJsonPtr)
            free(statusJsonPtr)

            guard let state = try? JSONSerialization.jsonObject(with: Data(statusJsonString.utf8), options: []) as? [String: Any] else {
                throw ColibriError.invalidJSON
            }

            guard let status = state["status"] as? String else {
                throw ColibriError.invalidJSON
            }

            switch status {
            case "success":
                // Success: return the result (could be any JSON type)
                return state["result"] // Returns Any? which matches function signature
            case "error":
                let errorMsg = state["error"] as? String ?? "Unknown verifier error"
                throw ColibriError.proofError("Verifier error for method \(method): \(errorMsg)")
            case "pending":
                guard let requests = state["requests"] as? [[String: Any]] else {
                    throw ColibriError.invalidJSON
                }
                // Call handleRequests, enabling the proofer fallback logic
                try await handleRequests(requests, useProoferFallback: true)
            default:
                throw ColibriError.unknownStatus(status)
            }
        }
    }

    // Implement the rpc method
    public func rpc(method: String, params: String) async throws -> Any {
        let methodType = try await getMethodSupport(method: method)
        var proof = Data()

        switch methodType {
        case .PROOFABLE:
            // Assuming params is a JSON string representing an array or object
            // We prefer fetching from a proofer if available
            if !proofers.isEmpty {
                 proof = try await fetchRpc(urls: proofers, method: method, params: params, asProof: true)
            } else {
                 proof = try await createProof(method: method, params: params)
            }
            // Verification happens below, after the switch

        case .UNPROOFABLE:
            let responseData = try await fetchRpc(urls: eth_rpcs, method: method, params: params, asProof: false)
            // Parse JSON response
            do {
                guard let jsonResponse = try JSONSerialization.jsonObject(with: responseData) as? [String: Any] else {
                    throw ColibriError.invalidJSON
                }
                if let error = jsonResponse["error"] as? [String: Any] {
                     let errorMessage = error["message"] as? String ?? "Unknown RPC error"
                     throw ColibriError.rpcError(errorMessage)
                }
                guard let result = jsonResponse["result"] else {
                    throw ColibriError.invalidJSON // Result field is missing
                }
                return result
            } catch let error as ColibriError {
                 throw error // Re-throw Colibri specific errors
            } catch {
                 throw ColibriError.invalidJSON // Catch JSON parsing errors
            }

        case .NOT_SUPPORTED:
            throw ColibriError.methodNotSupported(method)

        case .LOCAL:
            // For local methods, we still call verify with empty proof
            proof = Data()
            // Verification happens below, after the switch

        case .UNKNOWN:
             throw ColibriError.unknownMethodType(method)
        }

        // Verify the proof (either created/fetched for PROOFABLE, or empty for LOCAL)
        return try await verifyProof(proof: proof, method: method, params: params)
    }

    // Helper function to handle pending requests
    private func handleRequests(_ requests: [[String: Any]], useProoferFallback: Bool = false) async throws {
        await withTaskGroup(of: Void.self) { group in
            for request in requests {
                group.addTask {
                    guard let reqPtr = request["req_ptr"] as? UnsafeMutableRawPointer,
                          let uri = request["url"] as? String,
                          let method = request["method"] as? String,
                          let excludeMask = request["exclude_mask"] as? Int else {
                        return
                    }
                    
                    // Determine server list based on the flag and request type
                    let requestType = request["type"] as? String
                    let servers: [String]
                    if useProoferFallback && requestType == "beacon" && !self.proofers.isEmpty {
                        servers = self.proofers
                    } else if requestType == "beacon" {
                        servers = self.beacon_apis
                    } else {
                        servers = self.eth_rpcs
                    }
                    
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
                                let bytes = responseData.withUnsafeBytes { rawBufferPointer in
                                    bytes_t(
                                        data: UnsafeMutablePointer(mutating: rawBufferPointer.bindMemory(to: UInt8.self).baseAddress!),
                                        len: UInt32(responseData.count)
                                    )
                                }
                                c4_req_set_response(reqPtr, bytes, Int32(index))
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
                        c4_req_set_error(reqPtr, errorCStr, 0)
                        free(errorCStr)
                    }
                }
            }
        }
    }

    // Add the fetchRpc helper function
    private func fetchRpc(urls: [String], method: String, params: String, asProof: Bool) async throws -> Data {
        var lastError: Error = ColibriError.rpcError("All nodes failed") // Initialize with a default error

        // Prepare JSON RPC request body data once
        let jsonRpcBody: [String: Any] = [
            "id": 1,
            "jsonrpc": "2.0",
            "method": method,
            "params": try JSONSerialization.jsonObject(with: params.data(using: .utf8) ?? Data()) // Assume params is valid JSON string
        ]
        let httpBody = try JSONSerialization.data(withJSONObject: jsonRpcBody)

        for urlString in urls {
            guard let url = URL(string: urlString) else {
                print("Warning: Invalid URL string: \(urlString)")
                lastError = ColibriError.invalidURL
                continue // Try the next URL
            }

            var request = URLRequest(url: url)
            request.httpMethod = "POST"
            request.httpBody = httpBody
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.setValue(asProof ? "application/octet-stream" : "application/json", forHTTPHeaderField: "Accept")
            // Add a timeout
            request.timeoutInterval = 30 // 30 seconds timeout

            do {
                 print("Fetching \(method) from \(urlString)")
                let (responseData, response) = try await URLSession.shared.data(for: request)

                guard let httpResponse = response as? HTTPURLResponse else {
                    throw ColibriError.rpcError("Invalid response received")
                }
                
                print("Response status code from \(urlString): \(httpResponse.statusCode)")

                if (200...299).contains(httpResponse.statusCode) {
                     if !asProof {
                         // Basic check if it's likely JSON before returning Data
                         if responseData.isEmpty || responseData.first != UInt8(ascii: "{") {
                             print("Warning: Expected JSON response from \(urlString), but received non-JSON data.")
                             // Fallthrough to return Data, caller will handle parsing error
                         }
                     }
                    return responseData // Success
                } else {
                    let responseBodyString = String(data: responseData, encoding: .utf8) ?? "Non-UTF8 response body"
                    lastError = ColibriError.httpError(statusCode: httpResponse.statusCode, details: responseBodyString)
                    print("HTTP error \(httpResponse.statusCode) from \(urlString): \(responseBodyString)")
                    // Continue to try the next URL
                }
            } catch let error as URLError where error.code == .timedOut {
                 lastError = ColibriError.rpcError("Request to \(urlString) timed out")
                 print("Request timed out for \(urlString)")
                 // Continue to try the next URL
            } catch {
                 lastError = ColibriError.rpcError("Network or other error fetching from \(urlString): \(error.localizedDescription)")
                 print("Error fetching from \(urlString): \(error)")
                // Continue to try the next URL
            }
        }
        // If loop finishes without returning, all URLs failed
        throw lastError
    }

    // Helper extension for safe byte operations
    private extension Data {
        func copyBytes() -> [UInt8] {
            return [UInt8](self)
        }
        
        // Safe withUnsafeBytes wrapper for older Swift versions compatibility
        func safeWithUnsafeBytes<ResultType>(_ body: (UnsafeRawBufferPointer) throws -> ResultType) rethrows -> ResultType {
            return try withUnsafeBytes(body)
        }
    }
}

// Error enum for better error handling
public enum ColibriError: Error, LocalizedError, Equatable {
    case invalidInput
    case executionFailed
    case invalidJSON
    case proofError(String)
    case unknownStatus(String)
    case invalidURL
    case rpcError(String)
    case httpError(statusCode: Int, details: String)
    case methodNotSupported(String)
    case unknownMethodType(String)
    case memoryAllocationFailed
    case nullPointerReceived
    case contextCreationFailed
    
    public var errorDescription: String? {
        switch self {
        case .invalidInput:
            return "Ungültige Eingabeparameter"
        case .executionFailed:
            return "Ausführung fehlgeschlagen"
        case .invalidJSON:
            return "Ungültiges JSON-Format"
        case .proofError(let message):
            return "Proof-Fehler: \(message)"
        case .unknownStatus(let status):
            return "Unbekannter Status: \(status)"
        case .invalidURL:
            return "Ungültige URL"
        case .rpcError(let message):
            return "RPC-Fehler: \(message)"
        case .httpError(let statusCode, let details):
            return "HTTP-Fehler \(statusCode): \(details)"
        case .methodNotSupported(let method):
            return "Methode nicht unterstützt: \(method)"
        case .unknownMethodType(let method):
            return "Unbekannter Methodentyp für: \(method)"
        case .memoryAllocationFailed:
            return "Speicherallokation fehlgeschlagen"
        case .nullPointerReceived:
            return "Null-Pointer von C-Funktion erhalten"
        case .contextCreationFailed:
            return "Kontext-Erstellung fehlgeschlagen"
        }
    }
    
    public var failureReason: String? {
        switch self {
        case .invalidInput:
            return "Die übergebenen Parameter sind null oder ungültig."
        case .executionFailed:
            return "Die C-Bibliothek konnte die Operation nicht ausführen."
        case .invalidJSON:
            return "Die JSON-Daten konnten nicht geparst werden."
        case .proofError(let message):
            return "Bei der Proof-Generierung ist ein Fehler aufgetreten: \(message)"
        case .unknownStatus(let status):
            return "Die C-Bibliothek hat einen unbekannten Status zurückgegeben: \(status)"
        case .invalidURL:
            return "Die URL-Zeichenkette ist kein gültiges URL-Format."
        case .rpcError(let message):
            return "RPC-Aufruf fehlgeschlagen: \(message)"
        case .httpError(let statusCode, let details):
            return "HTTP-Request fehlgeschlagen mit Status \(statusCode): \(details)"
        case .methodNotSupported(let method):
            return "Die Methode \(method) wird für diese Chain nicht unterstützt."
        case .unknownMethodType(let method):
            return "Konnte Methodentyp für \(method) nicht bestimmen."
        case .memoryAllocationFailed:
            return "Nicht genügend Speicher verfügbar."
        case .nullPointerReceived:
            return "Unerwarteter null-Pointer von der C-Bibliothek."
        case .contextCreationFailed:
            return "Der Kontext für die Operation konnte nicht erstellt werden."
        }
    }
}

