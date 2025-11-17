/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "logger.h"
#include "server.h"
#include "state.h"
#include "util/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function for safe substring search in bytes_t
static bool bytes_contains_string(bytes_t data, const char* needle) {
  if (!data.data || !needle || data.len == 0) return false;

  size_t needle_len = strlen(needle);
  if (needle_len > data.len) return false;

// Use memmem if available, otherwise manual search
#ifdef __GLIBC__
  return memmem(data.data, data.len, needle, needle_len) != NULL;
#else
  // Manual search for portability
  for (size_t i = 0; i <= data.len - needle_len; i++) {
    if (memcmp(data.data + i, needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
#endif
}

// Quick helper: check if a JSON-RPC request is for a specific method without full parse
static bool req_is_method(data_request_t* req, const char* method_name) {
  if (!req || req->type != C4_DATA_TYPE_ETH_RPC || !req->payload.data || req->payload.len == 0 || !method_name) return false;
  // Look for a compact pattern to avoid allocating: "method":"<name>"
  char pattern[128];
  int  n = snprintf(pattern, sizeof(pattern), "\"method\":\"%s\"", method_name);
  if (n <= 0 || (size_t) n >= sizeof(pattern)) return false;
  return bytes_contains_string(req->payload, pattern);
}

// Helper function to set JSON-RPC error message from error object
static void set_jsonrpc_error_message(data_request_t* req, json_t error, int error_code, const char* prefix) {
  if (req->error) return; // Don't overwrite existing error

  json_t msg_json = json_get(error, "message");
  if (msg_json.type == JSON_TYPE_STRING) {
    char* msg = json_as_string(msg_json, NULL);
    if (msg) {
      req->error = bprintf(NULL, "%s (code: %d): %s", prefix, error_code, msg);
      free(msg);
      return;
    }
  }
  // Fallback if no message found
  req->error = bprintf(NULL, "%s (code: %d)", prefix, error_code);
}

// Helper function to set JSON-RPC error message without code
static void set_jsonrpc_simple_error_message(data_request_t* req, json_t error, const char* fallback) {
  if (req->error) return; // Don't overwrite existing error

  json_t msg_json = json_get(error, "message");
  if (msg_json.type == JSON_TYPE_STRING) {
    char* msg = json_as_string(msg_json, NULL);
    if (msg) {
      req->error = bprintf(NULL, "JSON-RPC error: %s", msg);
      free(msg);
      return;
    }
  }
  // Fallback message
  req->error = strdup(fallback);
}

// Helper function to classify JSON-RPC error by code
static c4_response_type_t classify_jsonrpc_error_by_code(int error_code, json_t error, data_request_t* req) {
  // JSON-RPC error codes classification based on QuickNode documentation:
  // https://www.quicknode.com/docs/ethereum/error-references#evm-rpc-error-codes

  switch (error_code) {
    // Standard JSON-RPC errors (mostly user errors)
    case -32700: // Parse error - Invalid JSON
      return C4_RESPONSE_ERROR_USER;

    case -32600: { // Invalid request - malformed request or tier limitation
      json_t message = json_get(error, "message");
      if (message.type == JSON_TYPE_STRING) {
        bytes_t msg_bytes = bytes(message.start, message.len);
        // Check for tier/subscription limitations that should be treated as method not supported
        if (bytes_contains_string(msg_bytes, "not available on the Free tier") ||
            bytes_contains_string(msg_bytes, "upgrade to Pay As You Go") ||
            bytes_contains_string(msg_bytes, "Enterprise for access") ||
            bytes_contains_string(msg_bytes, "subscription plan") ||
            bytes_contains_string(msg_bytes, "tier limitation") ||
            bytes_contains_string(msg_bytes, "plan does not support")) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC method not available on current tier");
          return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;
        }
      }
      // Other -32600 errors are user errors (malformed JSON, etc.)
      return C4_RESPONSE_ERROR_USER;
    }

    case -32601: // Method not found -> treat as method not supported for this server
      return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;

    case -32602: { // Invalid params - needs message analysis
      json_t message = json_get(error, "message");
      if (message.type == JSON_TYPE_STRING) {
        bytes_t msg_bytes = bytes(message.start, message.len);
        // Missing 0x prefix or block range limits are user errors
        if (bytes_contains_string(msg_bytes, "missing 0x prefix") ||
            bytes_contains_string(msg_bytes, "Block range limit exceeded") ||
            bytes_contains_string(msg_bytes, "invalid") ||
            bytes_contains_string(msg_bytes, "missing") ||
            bytes_contains_string(msg_bytes, "wrong")) {
          return C4_RESPONSE_ERROR_USER;
        }
        // eth_getProof specific: providers that don't serve historical proofs
        if (req_is_method(req, "eth_getProof") && (bytes_contains_string(msg_bytes, "distance to target block exceeds maximum proof window") ||
                                                   bytes_contains_string(msg_bytes, "proof window") ||
                                                   bytes_contains_string(msg_bytes, "only latest state") ||
                                                   bytes_contains_string(msg_bytes, "state not available") ||
                                                   bytes_contains_string(msg_bytes, "state unavailable") ||
                                                   bytes_contains_string(msg_bytes, "proofs are only available for latest"))) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC method not available for requested state");
          return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;
        }
      }
      // Other -32602 errors might be server incompatibility - retry
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC invalid params");
      return C4_RESPONSE_ERROR_RETRY;
    }

    case -32603: // Internal JSON-RPC error
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC internal error");
      return C4_RESPONSE_ERROR_RETRY;

    // EVM/Ethereum specific errors
    case -32000: { // Generic server error - needs message analysis
      json_t message = json_get(error, "message");
      if (message.type == JSON_TYPE_STRING) {
        bytes_t msg_bytes = bytes(message.start, message.len);
        // Tier/subscription limitations - method not supported
        if (bytes_contains_string(msg_bytes, "not available on the Free tier") ||
            bytes_contains_string(msg_bytes, "upgrade to Pay As You Go") ||
            bytes_contains_string(msg_bytes, "Enterprise for access") ||
            bytes_contains_string(msg_bytes, "subscription plan") ||
            bytes_contains_string(msg_bytes, "tier limitation") ||
            bytes_contains_string(msg_bytes, "plan does not support") ||
            bytes_contains_string(msg_bytes, "method not supported") ||
            bytes_contains_string(msg_bytes, "feature not enabled") ||
            bytes_contains_string(msg_bytes, "API key is not allowed to access method")) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC method not available on current tier");
          return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;
        }
        // eth_getProof: historical state not served → treat as method-not-supported for this provider
        if (req_is_method(req, "eth_getProof") && (bytes_contains_string(msg_bytes, "distance to target block exceeds maximum proof window") ||
                                                   bytes_contains_string(msg_bytes, "proof window") ||
                                                   bytes_contains_string(msg_bytes, "only latest state") ||
                                                   bytes_contains_string(msg_bytes, "state not available") ||
                                                   bytes_contains_string(msg_bytes, "state unavailable") ||
                                                   bytes_contains_string(msg_bytes, "root hash mismatch") ||
                                                   bytes_contains_string(msg_bytes, "witnessTrieRootHash") ||
                                                   bytes_contains_string(msg_bytes, "expectedRootHash"))) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC method not available for requested state");
          return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;
        }
        // Sync-related errors - retryable
        else if (bytes_contains_string(msg_bytes, "Header not found") ||
                 bytes_contains_string(msg_bytes, "Block not found") ||
                 bytes_contains_string(msg_bytes, "not in sync") ||
                 bytes_contains_string(msg_bytes, "block number is in the future")) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC sync error");
          return C4_RESPONSE_ERROR_RETRY;
        }
        // Timeout errors - retryable
        else if (bytes_contains_string(msg_bytes, "Execution timeout") ||
                 bytes_contains_string(msg_bytes, "timeout")) {
          set_jsonrpc_error_message(req, error, error_code, "JSON-RPC timeout");
          return C4_RESPONSE_ERROR_RETRY;
        }
        // User errors - not retryable
        else if (bytes_contains_string(msg_bytes, "Nonce too low") ||
                 bytes_contains_string(msg_bytes, "Gas limit") ||
                 bytes_contains_string(msg_bytes, "Transaction cost exceeds")) {
          return C4_RESPONSE_ERROR_USER;
        }
      }
      // Default -32000 handling - treat as retryable server error
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC server error");
      return C4_RESPONSE_ERROR_RETRY;
    }

    case -32001: // Resource not found
    case -32002: // Resource unavailable
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC resource unavailable");
      return C4_RESPONSE_ERROR_RETRY;

    case -32003: // Transaction rejected
      return C4_RESPONSE_ERROR_USER;

    case -32004: // Method not supported
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC method not supported");
      return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;

    case -32005: // Limit exceeded
      return C4_RESPONSE_ERROR_RETRY;

    case -32029: // Too Many Requests (provider-specific JSON-RPC code)
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC rate limited");
      return C4_RESPONSE_ERROR_RETRY;

    case -32009: // Trace requests limited
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC trace limited");
      return C4_RESPONSE_ERROR_RETRY;

    case -32011: // Network error
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC network error");
      return C4_RESPONSE_ERROR_RETRY;

    case -32015: // VM execution error
      return C4_RESPONSE_ERROR_USER;

    case 3: // Execution reverted
      return C4_RESPONSE_ERROR_USER;

    // Default: Unknown error codes - treat as retryable server errors
    default:
      set_jsonrpc_error_message(req, error, error_code, "JSON-RPC unknown error");
      return C4_RESPONSE_ERROR_RETRY;
  }
}

// Helper function to classify JSON-RPC errors
static c4_response_type_t classify_jsonrpc_error(json_t error, data_request_t* req) {
  c4_response_type_t result = C4_RESPONSE_ERROR_RETRY;

  if (error.type == JSON_TYPE_OBJECT) {
    json_t code = json_get(error, "code");
    if (code.type == JSON_TYPE_NUMBER) {
      // JSON-RPC error codes are typically negative, parse as signed integer
      char* code_str   = json_as_string(code, NULL);
      int   error_code = code_str ? atoi(code_str) : 0;
      if (code_str) safe_free(code_str);
      result = classify_jsonrpc_error_by_code(error_code, error, req);

      // Log the classification result with error code
      //      fprintf(stderr, "   [jsonrpc] JSON-RPC error (code: %d) - %s\n",
      //              error_code,
      //              result == C4_RESPONSE_ERROR_USER ? "not retryable" : "retryable");
      return result;
    }
    // Error object without code - treat as server error
    set_jsonrpc_simple_error_message(req, error, "JSON-RPC error without code");
    log_warn("   [jsonrpc] JSON-RPC error without code - retryable");
    return C4_RESPONSE_ERROR_RETRY;
  }
  else if (error.type == JSON_TYPE_STRING) {
    // String error - treat as server error
    if (!req->error) {
      char* msg = json_as_string(error, NULL);
      if (msg) {
        req->error = bprintf(NULL, "JSON-RPC error: %s", msg);
        safe_free(msg);
      }
      else
        req->error = strdup("JSON-RPC string error");
    }
    log_warn("   [jsonrpc] JSON-RPC string error - retryable");
    return C4_RESPONSE_ERROR_RETRY;
  }

  // Unknown error type - should not happen
  return C4_RESPONSE_ERROR_RETRY;
}

// Helper function to check for Beacon API sync lag
static bool c4_is_beacon_api_sync_lag(long http_code, const char* url, bytes_t response_body) {
  if (http_code != 404 || !url) return false;

  // Check if this is a beacon API endpoint (blocks/headers/historical/clients) or light-client endpoints
  bool is_beacon_path = (strstr(url, "/beacon/blocks/") || strstr(url, "/beacon/headers/") ||
                         strstr(url, "/historical_summaries/") || strstr(url, "/nimbus/") || strstr(url, "/lodestar/") ||
                         strstr(url, "/eth/v1/beacon/light_client/bootstrap/") ||
                         strstr(url, "/eth/v1/beacon/light_client/updates/"));
  if (!is_beacon_path) return false;

  if (!response_body.data || response_body.len == 0) return false;

  // Treat common not-available signals as sync/availability lag → retryable
  return (
      bytes_contains_string(response_body, "Block header/data has not been found") ||
      bytes_contains_string(response_body, "Block not found") ||
      bytes_contains_string(response_body, "Header not found") ||
      bytes_contains_string(response_body, "block not found") ||
      bytes_contains_string(response_body, "header not found") ||
      bytes_contains_string(response_body, "unknown block") ||
      bytes_contains_string(response_body, "unknown header") ||
      // Light client bootstrap specific
      bytes_contains_string(response_body, "bootstrap unavailable") ||
      bytes_contains_string(response_body, "LC bootstrap unavailable"));
}

// Main function to classify response based on HTTP code and content
c4_response_type_t c4_classify_response(long http_code, const char* url, bytes_t response_body, data_request_t* req) {

  // Check for success first
  if (http_code >= 200 && http_code < 300) {
    // For JSON-RPC, we need to check for error field even with 200 status
    if (req && req->type == C4_DATA_TYPE_ETH_RPC && response_body.data && response_body.len > 0) {
      // Treat result:null as retryable for certain methods where null indicates unavailability
      // rather than a valid absence. This avoids false positives when a lagging node returns null.
      bool has_null_result = bytes_contains_string(response_body, "\"result\":null");
      if (has_null_result) {
        bool null_retry =
            req_is_method(req, "eth_getBlockReceipts") ||
            req_is_method(req, "eth_getBlockByHash") ||
            req_is_method(req, "eth_getBlockByNumber");
        if (null_retry) {
          if (!req->error) req->error = strdup("JSON-RPC result is null");
          log_warn("   [json ] Treating result=null as retryable for this method");
          return C4_RESPONSE_ERROR_RETRY;
        }
      }
      // Quick check: only parse JSON if "error" appears in first 100 bytes
      // This avoids expensive JSON parsing for successful responses
      if (bytes_contains_string(bytes_slice(response_body, 0, response_body.len < 100 ? response_body.len : 100), "\"error\"")) {
        json_t response = json_parse((char*) response_body.data);
        if (response.type == JSON_TYPE_OBJECT) {
          json_t error = json_get(response, "error");
          if (error.type != JSON_TYPE_NOT_FOUND)
            return classify_jsonrpc_error(error, req);
        }
      }
    }
    // No error found or not JSON-RPC - success
    return C4_RESPONSE_SUCCESS;
  }
  // Handle HTTP error codes
  // All 5xx codes are server errors (retryable) and <400 are curl/transport considered retryable
  if (http_code >= 500 || http_code < 400) return C4_RESPONSE_ERROR_RETRY;

  // Server configuration/infrastructure errors
  // 401/429 remain retryable, but 403 may encode tier/method limitations — try to parse JSON if present
  if (http_code == 401 || http_code == 429) return C4_RESPONSE_ERROR_RETRY;
  if (http_code == 403 && req && req->type == C4_DATA_TYPE_ETH_RPC && response_body.data && response_body.len > 0 && bytes_contains_string(response_body, "\"error\"")) {
    json_t response = json_parse((char*) response_body.data);
    if (response.type == JSON_TYPE_OBJECT) {
      json_t error = json_get(response, "error");
      if (error.type != JSON_TYPE_NOT_FOUND)
        return classify_jsonrpc_error(error, req);
    }
    return C4_RESPONSE_ERROR_RETRY; // Fallback if parsing failed
  }

  // Special handling for HTTP 400 with JSON-RPC errors - check if it's a method not supported error
  if (http_code == 400 && req && req->type == C4_DATA_TYPE_ETH_RPC && response_body.data && response_body.len > 0) {
    // Quick check: only parse JSON if "error" appears in response
    if (bytes_contains_string(response_body, "\"error\"")) {
      json_t response = json_parse((char*) response_body.data);
      if (response.type == JSON_TYPE_OBJECT) {
        json_t error = json_get(response, "error");
        if (error.type != JSON_TYPE_NOT_FOUND)
          return classify_jsonrpc_error(error, req);
      }
    }
  }

  // Special handling for Beacon API sync lag
  if (req && req->type == C4_DATA_TYPE_BEACON_API && c4_is_beacon_api_sync_lag(http_code, url, response_body)) {
    log_warn("   [sync ] Detected potential sync lag for beacon API - treating as server error, not user error");
    return C4_RESPONSE_ERROR_RETRY;
  }

  // Special handling for HTTP 400 with JSON-RPC errors - check if it's a method not supported error
  if (http_code == 400 && req && req->type == C4_DATA_TYPE_BEACON_API && response_body.data && response_body.len > 0) {
    // Quick check: only parse JSON if "error" appears in response
    if (bytes_contains_string(response_body, "Unsupported method"))
      return C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED;
    else
      return C4_RESPONSE_ERROR_USER;
  }

  // All other 4xx codes are user errors
  return C4_RESPONSE_ERROR_USER;
}

// Public helper: conservative detection whether the error indicates "not found"
// across JSON-RPC and Beacon responses.
bool c4_error_indicates_not_found(long http_code, data_request_t* req, bytes_t response_body) {
  if (http_code == 404) return true;
  if (!req) return false;
  if (req->type == C4_DATA_TYPE_ETH_RPC && response_body.data && response_body.len > 0) {
    if (bytes_contains_string(response_body, "\"error\"")) {
      json_t response = json_parse((char*) response_body.data);
      if (response.type == JSON_TYPE_OBJECT) {
        json_t error = json_get(response, "error");
        json_t code  = json_get(error, "code");
        json_t msg   = json_get(error, "message");
        int    ec    = 0;
        if (code.type == JSON_TYPE_NUMBER) {
          char* cs = json_as_string(code, NULL);
          if (cs) {
            ec = atoi(cs);
            safe_free(cs);
          }
        }
        // Known not-found patterns in message
        bool msg_nf = false;
        if (msg.type == JSON_TYPE_STRING) {
          bytes_t m = bytes(msg.start, msg.len);
          msg_nf    = bytes_contains_string(m, "not found") || bytes_contains_string(m, "Header not found") || bytes_contains_string(m, "Block not found");
        }
        if (ec == -32601 || msg_nf) return true; // method not found or explicit not found
      }
    }
  }
  // Beacon API: reuse sync-lag detection as a proxy for not found
  if (req->type == C4_DATA_TYPE_BEACON_API) {
    return c4_is_beacon_api_sync_lag(http_code, req->url, response_body);
  }
  return false;
}
