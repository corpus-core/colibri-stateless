import Foundation
import CColibriMacOS

// MARK: - Storage System

/// Protocol for storage operations (similar to Android ColibriStorage)
public protocol ColibriStorage {
    func get(key: String) -> Data?
    func set(key: String, value: Data)
    func delete(key: String)
}

/// Default file storage implementation (similar to C FILE_STORAGE)
private class DefaultFileStorage: ColibriStorage {
    private let baseDirectory: URL
    
    init() {
        // Use C4_STATES_DIR environment variable or current directory
        if let statesDir = ProcessInfo.processInfo.environment["C4_STATES_DIR"] {
            baseDirectory = URL(fileURLWithPath: statesDir)
        } else {
            baseDirectory = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        }
        
        // Ensure directory exists
        try? FileManager.default.createDirectory(at: baseDirectory, withIntermediateDirectories: true)
        print("🗄️ Default Storage: Using directory \(baseDirectory.path)")
    }
    
    func get(key: String) -> Data? {
        let fileURL = baseDirectory.appendingPathComponent(key)
        
        do {
            let data = try Data(contentsOf: fileURL)
            print("🗄️ Default Storage GET: \(key) (\(data.count) bytes)")
            return data
        } catch {
            // File not found is normal for storage
            return nil
        }
    }
    
    func set(key: String, value: Data) {
        let fileURL = baseDirectory.appendingPathComponent(key)
        
        do {
            try value.write(to: fileURL)
            print("🗄️ Default Storage SET: \(key) (\(value.count) bytes)")
        } catch {
            print("🗄️ Default Storage SET ERROR: \(key) - \(error)")
        }
    }
    
    func delete(key: String) {
        let fileURL = baseDirectory.appendingPathComponent(key)
        
        do {
            try FileManager.default.removeItem(at: fileURL)
            print("🗄️ Default Storage DELETE: \(key)")
        } catch {
            // File not found is normal for delete
        }
    }
}

/// Singleton to hold the storage implementation
public class StorageBridge {
    public static var implementation: ColibriStorage?
    private static var isInitialized = false
    
    /// Register a storage implementation
    public static func registerStorage(_ storage: ColibriStorage) {
        implementation = storage
        print("🗄️ Swift Storage implementation registered")
        
        // Initialize C bridge if not already done
        if !isInitialized {
            initializeStorageBridge()
            isInitialized = true
        }
    }
    
    /// Ensure storage is initialized with default if needed
    public static func ensureStorageInitialized() {
        if implementation == nil {
            print("🗄️ No storage implementation set, using default file storage")
            registerStorage(DefaultFileStorage())
        }
    }
    
    /// Initialize the C storage bridge with Swift callbacks
    private static func initializeStorageBridge() {
        // Register Swift callback functions with C bridge
        swift_storage_bridge_register_get(swift_storage_get_callback)
        swift_storage_bridge_register_set(swift_storage_set_callback)
        swift_storage_bridge_register_delete(swift_storage_delete_callback)
        
        // Initialize the C storage plugin
        swift_storage_bridge_initialize()
        
        print("🗄️ Storage bridge initialized with Swift callbacks")
    }
}

// MARK: - Swift Storage Callbacks (called from C)

/// Swift callback function for storage get (called from C)
let swift_storage_get_callback: @convention(c) (UnsafePointer<CChar>?, UnsafeMutablePointer<UInt32>?) -> UnsafeMutableRawPointer? = { key, out_len in
    StorageBridge.ensureStorageInitialized()  // Auto-initialize default storage if needed
    
    guard let implementation = StorageBridge.implementation,
          let key = key,
          let out_len = out_len else {
        print("🗄️ Storage get called but no implementation registered or nil parameters")
        return nil
    }
    
    let keyString = String(cString: key)
    guard let data = implementation.get(key: keyString) else {
        // Key not found
        out_len.pointee = 0
        return nil
    }
    
    // Allocate C memory and copy Swift Data
    let buffer = malloc(data.count)
    if let buffer = buffer {
        data.withUnsafeBytes { bytes in
            memcpy(buffer, bytes.baseAddress, data.count)
        }
        out_len.pointee = UInt32(data.count)
        return buffer
    } else {
        print("🗄️ Storage get: Failed to allocate memory for key \(keyString)")
        out_len.pointee = 0
        return nil
    }
}

/// Swift callback function for storage set (called from C)
let swift_storage_set_callback: @convention(c) (UnsafePointer<CChar>?, UnsafePointer<UInt8>?, UInt32) -> Void = { key, data, len in
    StorageBridge.ensureStorageInitialized()  // Auto-initialize default storage if needed
    
    guard let implementation = StorageBridge.implementation,
          let key = key,
          let data = data else {
        print("🗄️ Storage set called but no implementation registered or nil parameters")
        return
    }
    
    let keyString = String(cString: key)
    let dataBuffer = Data(bytes: data, count: Int(len))
    implementation.set(key: keyString, value: dataBuffer)
}

/// Swift callback function for storage delete (called from C)
let swift_storage_delete_callback: @convention(c) (UnsafePointer<CChar>?) -> Void = { key in
    StorageBridge.ensureStorageInitialized()  // Auto-initialize default storage if needed
    
    guard let implementation = StorageBridge.implementation,
          let key = key else {
        print("🗄️ Storage delete called but no implementation registered or nil key")
        return
    }
    
    let keyString = String(cString: key)
    implementation.delete(key: keyString)
}

// MARK: - Mock System

/// Protocol for mocking HTTP requests in tests
public protocol RequestHandler {
    func handleRequest(_ request: DataRequest) async throws -> Data
}

/// Represents a data request that can be mocked
public struct DataRequest {
    public let url: String
    public let method: String
    public let payload: [String: Any]?
    public let encoding: String?
    public let type: String?
    
    public init(url: String, method: String, payload: [String: Any]? = nil, encoding: String? = nil, type: String? = nil) {
        self.url = url
        self.method = method
        self.payload = payload
        self.encoding = encoding
        self.type = type
    }
}

// MARK: - Method Types
public enum MethodType: Int, CaseIterable {
    case UNKNOWN = 0
    case PROOFABLE = 1
    case UNPROOFABLE = 2
    case NOT_SUPPORTED = 3
    case LOCAL = 4
    
    public var description: String {
        switch self {
        case .UNKNOWN: return "Unknown"
        case .PROOFABLE: return "Proofable"
        case .UNPROOFABLE: return "Unproofable" 
        case .NOT_SUPPORTED: return "Not Supported"
        case .LOCAL: return "Local"
        }
    }
}

public class Colibri {
    // Configuration with defaults
    public var eth_rpcs: [String] = []
    public var beacon_apis: [String] = []
    public var provers: [String] = ["https://c4.incubed.net"]
    public var checkpointz: [String] = ["https://sync-mainnet.beaconcha.in", "https://beaconstate.info", "https://sync.invis.tools", "https://beaconstate.ethstaker.cc"]
    public var trustedCheckpoint: String? = nil
    public var chainId: UInt64 = 1 // Default: Ethereum Mainnet
    public var includeCode: Bool = false
    
    /// Optional request handler for mocking HTTP requests in tests
    public var requestHandler: RequestHandler?

    public init() {}

    public static func initialize() {
        // Placeholder for initialization if needed
    }

    // MARK: - Method Support
    
    /// Check if a method is supported for proof generation
    public func getMethodSupport(method: String) -> MethodType {
        let methodPtr = method.withCString { strdup($0) }
        guard let methodCStr = methodPtr else {
            return .UNKNOWN
        }
        defer { free(methodCStr) }

        let typeRaw = c4_get_method_support(chainId, methodCStr)
        guard let type = MethodType(rawValue: Int(typeRaw)) else {
             // Handle cases where the C function might return an unexpected value
            print("Warning: Unknown method type raw value \(typeRaw) returned from c4_get_method_support for method \(method)")
            return .UNKNOWN
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
        
        guard let ctx = c4_create_prover_ctx(methodPtr, paramsPtr, chainId, includeCode ? 1 : 0) else {
            throw ColibriError.contextCreationFailed
        }
        defer { c4_free_prover_ctx(ctx) }

        while true {
            guard let jsonStatusPtr = c4_prover_execute_json_status(ctx) else {
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
                let proof = c4_prover_get_proof(ctx)
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
                try await handleRequests(requests, useProverFallback: false)
            default:
                throw ColibriError.unknownStatus(status)
            }
        }
    }

    // Verify proof asynchronously
    public func verifyProof(proof: Data, method: String, params: String) async throws -> Any {
        // Handle optional trusted checkpoint
        let trustedCheckpointCStr: UnsafeMutablePointer<CChar>?
        if let checkpoint = trustedCheckpoint {
            trustedCheckpointCStr = checkpoint.withCString { strdup($0) }
        } else {
            trustedCheckpointCStr = nil
        }
        
        let methodPtr = method.withCString { strdup($0) }
        let paramsPtr = params.withCString { strdup($0) }
        
        guard let methodCStr = methodPtr,
              let paramsCStr = paramsPtr else {
            if let ptr = trustedCheckpointCStr {
                free(ptr)
            }
            throw ColibriError.invalidInput
        }
        
        defer {
            free(methodPtr)
            free(paramsPtr)
            if let ptr = trustedCheckpointCStr {
                free(ptr)
            }
        }
        
        // Create bytes_t struct for proof data with safe memory handling
        let proofBytes = proof.withUnsafeBytes { rawBufferPointer in
            bytes_t(
                len: UInt32(proof.count),
                data: UnsafeMutablePointer(mutating: rawBufferPointer.bindMemory(to: UInt8.self).baseAddress!)
            )
        }
        
        guard let ctx = c4_verify_create_ctx(proofBytes, methodCStr, paramsCStr, chainId, trustedCheckpointCStr) else {
            throw ColibriError.contextCreationFailed
        }
        defer { c4_verify_free_ctx(ctx) }

        var iteration = 0
        let _ = 10 // maxIterations defined but not used in while true loop
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
                return state["result"] as Any // Explicitly cast to Any
            case "error":
                let errorMsg = state["error"] as? String ?? "Unknown verifier error"
                throw ColibriError.proofError("Verifier error for method \(method): \(errorMsg)")
            case "pending":
                guard let requests = state["requests"] as? [[String: Any]] else {
                    throw ColibriError.invalidJSON
                }
                // Call handleRequests, enabling the prover fallback logic
                try await handleRequests(requests, useProverFallback: true)
            default:
                throw ColibriError.unknownStatus(status)
            }
        }
    }

    // Implement the rpc method
    public func rpc(method: String, params: String) async throws -> Any {
        let methodType = getMethodSupport(method: method)
        var proof = Data()

        switch methodType {
        case .PROOFABLE:
            // Assuming params is a JSON string representing an array or object
            // We prefer fetching from a prover if available
            if !provers.isEmpty {
                 proof = try await fetchRpc(urls: provers, method: method, params: params, asProof: true)
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
    private func handleRequests(_ requests: [[String: Any]], useProverFallback: Bool = false) async throws {
        await withTaskGroup(of: Void.self) { group in
            for request in requests {
                group.addTask {
                    // Optional debug: Print request structure (uncomment for debugging)
                    // print("🔍 DEBUG handleRequests: Request keys: \(request.keys)")
                    // for (key, value) in request {
                    //     print("🔍 DEBUG handleRequests: \(key) = \(value) (type: \(type(of: value)))")
                    // }
                    
                    // Extract and convert types properly
                    guard let uri = request["url"] as? String,
                          let method = request["method"] as? String else {
                        print("❌ ERROR: Missing url/method in request: \(request)")
                        return
                    }
                    
                    // Convert req_ptr from NSNumber to UnsafeMutableRawPointer
                    let reqPtr: UnsafeMutableRawPointer
                    if let reqPtrNum = request["req_ptr"] as? NSNumber {
                        let reqPtrInt = reqPtrNum.int64Value
                        guard let ptr = UnsafeMutableRawPointer(bitPattern: UInt(reqPtrInt)) else {
                            print("❌ ERROR: Invalid req_ptr conversion from NSNumber \(reqPtrNum)")
                            return
                        }
                        reqPtr = ptr
                    } else {
                        print("❌ ERROR: req_ptr not NSNumber in request: \(request)")
                        return
                    }
                    
                    // Convert exclude_mask from String to Int
                    let excludeMask: Int
                    if let excludeMaskStr = request["exclude_mask"] as? String {
                        excludeMask = Int(excludeMaskStr) ?? 0
                    } else if let excludeMaskNum = request["exclude_mask"] as? NSNumber {
                        excludeMask = excludeMaskNum.intValue
                    } else {
                        print("❌ ERROR: exclude_mask neither String nor NSNumber in request: \(request)")
                        let errorMsg = "Invalid exclude_mask type"
                        errorMsg.withCString { errorCStr in
                            c4_req_set_error(reqPtr, UnsafeMutablePointer(mutating: errorCStr), 0)
                        }
                        return
                    }
                    
                    // Determine server list based on the flag and request type
                    let requestType = request["type"] as? String
                    let servers: [String]
                    if requestType == "checkpointz" {
                        servers = self.checkpointz
                    } else if useProverFallback && requestType == "beacon" && !self.provers.isEmpty {
                        servers = self.provers
                    } else if requestType == "beacon" {
                        servers = self.beacon_apis
                    } else {
                        servers = self.eth_rpcs
                    }
                    
                    // 🎯 MOCK SUPPORT: Check if request handler is set
                    if let requestHandler = self.requestHandler {
                        // Create DataRequest for mock handler
                        let dataRequest = DataRequest(
                            url: uri.isEmpty ? servers.first ?? "" : "\(servers.first ?? "")/\(uri)",
                            method: method,
                            payload: request["payload"] as? [String: Any],
                            encoding: request["encoding"] as? String,
                            type: requestType
                        )
                        
                        do {
                            let responseData = try await requestHandler.handleRequest(dataRequest)
                            let bytes = responseData.withUnsafeBytes { rawBufferPointer in
                                bytes_t(
                                    len: UInt32(responseData.count),
                                    data: UnsafeMutablePointer(mutating: rawBufferPointer.bindMemory(to: UInt8.self).baseAddress!)
                                )
                            }
                            c4_req_set_response(reqPtr, bytes, UInt16(0)) // Use index 0 for mocked responses
                            return
                        } catch {
                            let errorMsg = error.localizedDescription
                            let errorPtr = errorMsg.withCString { strdup($0) }
                            if let errorCStr = errorPtr {
                                c4_req_set_error(reqPtr, errorCStr, UInt16(0))
                                free(errorCStr)
                            }
                            return
                        }
                    }
                    
                    // REAL HTTP REQUEST: Continue with normal request handling
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
                            var urlRequest = URLRequest(url: url)
                            urlRequest.httpMethod = method
                            
                            // Set headers based on encoding type
                            if let encoding = request["encoding"] as? String {
                                if encoding == "json" {
                                    urlRequest.setValue("application/json", forHTTPHeaderField: "Accept")
                                } else {
                                    urlRequest.setValue("application/octet-stream", forHTTPHeaderField: "Accept")
                                }
                            }
                            
                            // Add payload if present
                            if let payload = request["payload"] as? [String: Any] {
                                urlRequest.setValue("application/json", forHTTPHeaderField: "Content-Type")
                                let payloadData = try JSONSerialization.data(withJSONObject: payload)
                                urlRequest.httpBody = payloadData
                            }
                            
                            let (responseData, response) = try await URLSession.shared.data(for: urlRequest)
                            
                            if let httpResponse = response as? HTTPURLResponse,
                               (200...299).contains(httpResponse.statusCode) {
                                // Success - set response and return
                                let bytes = responseData.withUnsafeBytes { rawBufferPointer in
                                    bytes_t(
                                        len: UInt32(responseData.count),
                                        data: UnsafeMutablePointer(mutating: rawBufferPointer.bindMemory(to: UInt8.self).baseAddress!)
                                    )
                                }
                                c4_req_set_response(reqPtr, bytes, UInt16(index))
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
                        c4_req_set_error(reqPtr, errorCStr, UInt16(0))
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

        // 🎯 MOCK SUPPORT: Check if request handler is set for direct RPC calls
        if let requestHandler = self.requestHandler {
            let dataRequest = DataRequest(
                url: urls.first ?? "",
                method: "POST",
                payload: jsonRpcBody,
                encoding: asProof ? "binary" : "json",
                type: "rpc"
            )
            
            return try await requestHandler.handleRequest(dataRequest)
        }
        
        // REAL HTTP REQUEST: Continue with normal implementation
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
}

// MARK: - Helper Extensions

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

// MARK: - Error Types

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

