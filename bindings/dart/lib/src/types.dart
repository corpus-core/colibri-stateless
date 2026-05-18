import 'dart:convert';

/// Pragmatic Adaptive Privacy mode.
///
/// [basic] sets `VERIFY_FLAG_PAP` (flag value 2) on verify and method-support
/// calls, which allows the native library to serve cached results locally when
/// possible.
enum PrivacyMode {
  none,
  basic,
}

/// Proof generation mode controlling how proofs are built and verified.
enum ProverMode {
  /// Proof built locally (requires Beacon API + execution client).
  local(0),
  /// Proof fetched entirely from remote prover server.
  remote(1),
  /// Header proof from remote server, execution data from RPC provider.
  hybrid(2),
  /// Like remote; client sends its RPC/Beacon URLs to the prover server.
  proxy(3),
  /// Like hybrid with automatic background header polling to keep the cache warm.
  lightClient(4);

  const ProverMode(this.value);

  /// Native integer value for this mode.
  final int value;
}

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

/// Thrown when an `eth_call` (or similar EVM execution) ran to completion but
/// reverted. The verifier has fully verified the revert -- this is a legitimate
/// outcome of EVM execution, not a transport or proof error.
///
/// Maps to the Geth-style RPC error `{ code: 3, message: "execution reverted",
/// data: "0x..." }`, which is also the EIP-1193 representation used by ethers
/// to decode `OffchainLookup` (EIP-3668 / CCIP-Read) and custom Solidity errors.
///
/// The raw revert bytes are exposed in [data] as a `0x`-prefixed hex string;
/// callers typically ABI-decode them with the contract's error definitions.
class RevertError extends RPCError {
  RevertError(this.data, {super.details})
      : super('execution reverted', code: 3);

  /// Raw EVM revert return-data as `0x`-prefixed hex (may be `0x` when empty).
  final String data;
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
  /// Creates a [DataRequest] from the given native request fields.
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

  /// Opaque native handle (pointer) for this request; used when fulfilling.
  final int reqPtr;
  /// Endpoint URL to call (e.g. RPC or beacon URL).
  final String url;
  /// HTTP or RPC method name (e.g. "eth_blockNumber").
  final String method;
  /// Response encoding (e.g. "json").
  final String encoding;
  /// Request type identifier from the native layer.
  final String requestType;
  /// Bitmask of servers to exclude when retrying.
  final int excludeMask;
  /// Chain ID (e.g. 1 for mainnet).
  final int chainId;
  /// Optional JSON payload for the request.
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
    Map<String, dynamic>? normalizedPayload;
    if (payload is Map<String, dynamic>) {
      normalizedPayload = payload;
    } else if (payload is String) {
      try {
        normalizedPayload = jsonDecode(payload) as Map<String, dynamic>;
      } on FormatException {
        normalizedPayload = null;
      }
    }

    return DataRequest(
      reqPtr: reqPtr,
      url: (data['url'] ?? '') as String,
      method: (data['method'] ?? 'get') as String,
      encoding: (data['encoding'] ?? 'json') as String,
      requestType: (data['type'] ?? 'eth_rpc') as String,
      excludeMask: excludeMask,
      chainId: () {
        final raw = data['chain_id'];
        if (raw == null) return 1;
        if (raw is num) return raw.toInt();
        return int.tryParse(raw.toString()) ?? 1;
      }(),
      payload: normalizedPayload,
    );
  }
}
