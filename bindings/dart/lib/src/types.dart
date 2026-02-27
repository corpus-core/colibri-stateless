import 'dart:convert';

/// Classification of how an RPC method is supported by Colibri.
enum MethodType {
  /// Method not recognized.
  undefined(0),
  /// Method supports proof generation and verification.
  proofable(1),
  /// Method is fetched directly from RPC without proof.
  unproofable(2),
  /// Method is not supported.
  notSupported(3),
  /// Method is verified locally (e.g. eth_chainId).
  local(4);

  const MethodType(this.value);
  /// Native integer value for this type.
  final int value;

  /// Converts a native integer into a [MethodType] value.
  ///
  /// Returns [undefined] for unknown values.
  static MethodType fromValue(int value) {
    for (final type in MethodType.values) {
      if (type.value == value) {
        return type;
      }
    }
    return MethodType.undefined;
  }
}

/// Base exception type for all Colibri Dart errors.
class ColibriError implements Exception {
  /// Creates an error with [message] and optional [details].
  ColibriError(this.message, {this.details});

  /// Short error message.
  final String message;
  /// Optional longer description.
  final String? details;

  @override
  String toString() {
    if (details == null || details!.isEmpty) {
      return message;
    }
    return '$message: $details';
  }
}

/// Thrown when proof creation fails.
class ProofError extends ColibriError {
  ProofError(super.message, {super.details});
}

/// Thrown when proof verification fails.
class VerificationError extends ColibriError {
  VerificationError(super.message, {super.details});
}

/// Thrown when an RPC call returns a JSON-RPC error.
class RPCError extends ColibriError {
  RPCError(super.message, {this.code, super.details});
  /// JSON-RPC error code when available.
  final int? code;
}

/// Thrown when HTTP transport fails for a data request.
class HTTPError extends ColibriError {
  HTTPError(super.message, {this.statusCode, super.details});
  /// HTTP status code when available.
  final int? statusCode;
}

/// A pending data request emitted by the native library during proof/verify.
///
/// Used internally when the prover or verifier needs external data (e.g. RPC
/// or beacon). Not typically used by callers directly.
class DataRequest {
  DataRequest({
    required this.reqPtr,
    required this.url,
    required this.method,
    required this.encoding,
    required this.requestType,
    required this.excludeMask,
    required this.chainId,
    this.payload,
  });

  final int reqPtr;
  final String url;
  final String method;
  final String encoding;
  final String requestType;
  final int excludeMask;
  final int chainId;
  final Map<String, dynamic>? payload;

  /// Parses a JSON request object returned by native status calls.
  static DataRequest fromJson(Map<String, dynamic> data) {
    final excludeRaw = data['exclude_mask'];
    final excludeMask = switch (excludeRaw) {
      int value => value,
      String value => int.tryParse(value) ?? 0,
      _ => 0,
    };

    final reqPtrRaw = data['req_ptr'];
    int parseReqPtr(dynamic raw) {
      if (raw is num) return raw.toInt();
      final s = raw.toString().replaceAll(RegExp(r'[^0-9]'), '');
      if (s.isEmpty) return 0;
      final big = BigInt.parse(s);
      final mask64 = (BigInt.one << 64) - BigInt.one;
      final u64 = big & mask64;
      final signBit = BigInt.one << 63;
      return u64 >= signBit ? (u64 - (BigInt.one << 64)).toInt() : u64.toInt();
    }
    final reqPtr = parseReqPtr(reqPtrRaw);

    final payload = data['payload'];
    final normalizedPayload = payload is Map<String, dynamic>
        ? payload
        : payload is String
            ? jsonDecode(payload) as Map<String, dynamic>
            : null;

    return DataRequest(
      reqPtr: reqPtr,
      url: (data['url'] ?? '') as String,
      method: (data['method'] ?? 'get') as String,
      encoding: (data['encoding'] ?? 'json') as String,
      requestType: (data['type'] ?? 'eth_rpc') as String,
      excludeMask: excludeMask,
      chainId: (data['chain_id'] ?? 1) is num
          ? (data['chain_id'] as num).toInt()
          : int.tryParse(data['chain_id'].toString()) ?? 1,
      payload: normalizedPayload,
    );
  }
}
