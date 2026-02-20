import 'dart:convert';

enum MethodType {
  undefined(0),
  proofable(1),
  unproofable(2),
  notSupported(3),
  local(4);

  const MethodType(this.value);
  final int value;

  static MethodType fromValue(int value) {
    for (final type in MethodType.values) {
      if (type.value == value) {
        return type;
      }
    }
    return MethodType.undefined;
  }
}

class ColibriError implements Exception {
  ColibriError(this.message, {this.details});

  final String message;
  final String? details;

  @override
  String toString() {
    if (details == null || details!.isEmpty) {
      return message;
    }
    return '$message: $details';
  }
}

class ProofError extends ColibriError {
  ProofError(super.message, {super.details});
}

class VerificationError extends ColibriError {
  VerificationError(super.message, {super.details});
}

class RPCError extends ColibriError {
  RPCError(super.message, {this.code, super.details});
  final int? code;
}

class HTTPError extends ColibriError {
  HTTPError(super.message, {this.statusCode, super.details});
  final int? statusCode;
}

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

  static DataRequest fromJson(Map<String, dynamic> data) {
    final excludeRaw = data['exclude_mask'];
    final excludeMask = switch (excludeRaw) {
      int value => value,
      String value => int.tryParse(value) ?? 0,
      _ => 0,
    };

    final reqPtrRaw = data['req_ptr'];
    final reqPtr = reqPtrRaw is num ? reqPtrRaw.toInt() : int.parse(reqPtrRaw.toString());

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
