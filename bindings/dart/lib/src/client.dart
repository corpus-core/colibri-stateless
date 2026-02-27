import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import 'native.dart';
import 'storage.dart';
import 'types.dart';

/// High-level Colibri client for proofed RPC calls.
///
/// Use [rpc] to run Ethereum (and compatible) RPC methods with automatic
/// proof generation and verification. Configure [provers], [ethRpcs], and
/// [beaconApis] for your network; optionally set [storage] for native cache.
/// Returns a hex string for up to [len] bytes of [proof] starting at [offset].
String _proofHexSnippet(Uint8List proof, int offset, int len) {
  if (proof.isEmpty) return '(empty)';
  final start = offset.clamp(0, proof.length);
  final end = (start + len).clamp(0, proof.length);
  if (start >= end) return '(out of range)';
  return proof.sublist(start, end).map((b) => b.toRadixString(16).padLeft(2, '0')).join(' ');
}

class Colibri {
  static const int _proverFlagZkProof = 1 << 7;

  /// Creates a client with optional custom endpoints and storage.
  ///
  /// [libraryPath] overrides the platform default native library location.
  /// [storage] registers Dart callbacks for the native cache layer.
  /// [zkProof] requests ZK sync proofs from remote provers when available.
  /// [checkpointWitnessKeys] provides signer keys for ZK proof verification.
  /// [onDebug] if set, called with short messages during rpc/verify (e.g. for UI log).
  Colibri({
    this.chainId = 1,
    List<String>? provers,
    List<String>? ethRpcs,
    List<String>? beaconApis,
    List<String>? checkpointz,
    this.trustedCheckpoint,
    this.includeCode = false,
    this.zkProof = false,
    this.checkpointWitnessKeys,
    this.logProverRequests = false,
    this.storage,
    void Function(String message)? onDebug,
    String? libraryPath,
    http.Client? httpClient,
  })  : provers = provers ?? _defaultProvers(chainId),
        _onDebug = onDebug,
        ethRpcs = ethRpcs ?? _defaultEthRpcs(chainId),
        beaconApis = beaconApis ?? _defaultBeaconApis(chainId),
        checkpointz = checkpointz ?? _defaultCheckpointz(chainId),
        _native = ColibriNative.load(libraryPath: libraryPath),
        _http = httpClient ?? http.Client() {
    _runtimeTrustedCheckpoint = trustedCheckpoint;
    final storageInstance = storage;
    if (storageInstance != null) {
      _native.registerStorage(storageInstance);
    }
  }

  /// Chain ID (e.g. 1 for mainnet, 11155111 for Sepolia).
  final int chainId;
  /// Prover endpoint URLs for remote proof fetching.
  final List<String> provers;
  /// Execution RPC URLs for unproofable methods and data.
  final List<String> ethRpcs;
  /// Beacon API URLs for checkpoint and light client data.
  final List<String> beaconApis;
  /// Checkpointz-style URLs for finalized checkpoint bootstrap.
  final List<String> checkpointz;
  /// Optional trusted checkpoint root (0x-prefixed hex).
  final String? trustedCheckpoint;
  /// Whether to include code in proof requests.
  final bool includeCode;
  /// Whether to request ZK sync proofs from provers.
  final bool zkProof;
  /// Optional witness signer keys for ZK proof verification.
  final String? checkpointWitnessKeys;
  /// Whether to log prover request parameters (debug).
  final bool logProverRequests;
  /// Optional storage backend for native cache.
  final ColibriStorage? storage;

  final void Function(String message)? _onDebug;
  final ColibriNative _native;
  final http.Client _http;
  String? _runtimeTrustedCheckpoint;
  bool _checkpointInitialized = false;

  /// Closes the underlying HTTP client.
  ///
  /// Call when the client is no longer needed to release resources.
  void close() {
    _http.close();
  }

  /// Returns how [method] is supported (proofable, local, unproofable, etc.).
  MethodType getMethodSupport(String method) {
    final support = _native.getMethodSupport(chainId, method);
    return MethodType.fromValue(support);
  }

  /// Creates a proof for [method] and [params] locally using the native library.
  ///
  /// Returns the serialized proof bytes. Use [verifyProof] to verify and get
  /// the result. Throws [ProofError] on failure.
  Future<Uint8List> createProof(String method, List<dynamic> params) async {
    final paramsJson = jsonEncode(params);
    var flags = includeCode ? 1 : 0;
    if (zkProof) {
      flags |= _proverFlagZkProof;
    }
    final ctx = _native.createProverCtx(
      method,
      paramsJson,
      chainId,
      flags,
    );

    if (ctx == ffi.nullptr) {
      throw ProofError('Failed to create prover context for $method');
    }

    try {
      while (true) {
        final statusJson = _native.proverExecuteJsonStatus(ctx);
        final status = jsonDecode(statusJson) as Map<String, dynamic>;

        switch (status['status']) {
          case 'success':
            return _native.proverGetProof(ctx);
          case 'error':
            throw ProofError(status['error']?.toString() ?? 'Unknown proof error');
          case 'pending':
            final requests = (status['requests'] as List<dynamic>? ?? [])
                .whereType<Map<String, dynamic>>()
                .map(DataRequest.fromJson)
                .toList();
            await _handleRequests(requests, useProverFallback: false);
            break;
          default:
            throw ProofError('Unknown prover status: ${status['status']}');
        }
      }
    } finally {
      _native.freeProverCtx(ctx);
    }
  }

  /// Verifies [proof] and returns the verified RPC result.
  ///
  /// Throws [VerificationError] if the proof is invalid.
  Future<dynamic> verifyProof(
    Uint8List proof,
    String method,
    List<dynamic> params,
  ) async {
    final paramsJson = jsonEncode(params);
    final checkpoint = _runtimeTrustedCheckpoint ?? trustedCheckpoint ?? '';
    _onDebug?.call(
      'Verifier call: method=$method paramsJson=$paramsJson chainId=$chainId '
      'checkpoint=${checkpoint.isEmpty ? "(empty)" : "${checkpoint.length} chars"} '
      'witnessKeys=${checkpointWitnessKeys ?? "null"}',
    );
    final ctx = _native.verifyCreateCtx(
      proof,
      method,
      paramsJson,
      chainId,
      checkpoint,
      witnessKeys: checkpointWitnessKeys,
    );

    if (ctx == ffi.nullptr) {
      throw VerificationError('Failed to create verification context for $method');
    }

    try {
      while (true) {
        final statusJson = _native.verifyExecuteJsonStatus(ctx);
        final status = jsonDecode(statusJson) as Map<String, dynamic>;

        switch (status['status']) {
          case 'success':
            return status['result'];
          case 'error':
            throw VerificationError(status['error']?.toString() ?? 'Unknown verification error');
          case 'pending':
            final requests = (status['requests'] as List<dynamic>? ?? [])
                .whereType<Map<String, dynamic>>()
                .map(DataRequest.fromJson)
                .toList();
            await _handleRequests(requests, useProverFallback: true);
            break;
          default:
            throw VerificationError('Unknown verifier status: ${status['status']}');
        }
      }
    } finally {
      _native.verifyFreeCtx(ctx);
    }
  }

  /// Executes an RPC call with automatic proof handling.
  ///
  /// For proofable methods, tries remote provers first (if configured), then
  /// falls back to local proof creation and verification. For local or
  /// unproofable methods, calls the appropriate path. Throws [ColibriError]
  /// or [RPCError] when the method is not supported or the call fails.
  Future<dynamic> rpc(String method, List<dynamic> params) async {
    final support = getMethodSupport(method);
    _onDebug?.call('Method: $method, support: ${support.name}');

    switch (support) {
      case MethodType.proofable:
        _onDebug?.call('Ensuring trusted checkpoint…');
        await _ensureTrustedCheckpoint();
        _onDebug?.call('Fetching proof (prover or local)…');
        final proof = await _fetchProofWithFallback(method, params);
        _onDebug?.call('Proof length: ${proof.length} bytes');
        _onDebug?.call('Proof (first 64 bytes hex): ${_proofHexSnippet(proof, 0, 64)}');
        _onDebug?.call('Proof (last 32 bytes hex): ${_proofHexSnippet(proof, proof.length - 32, 32)}');
        var result = await verifyProof(proof, method, params);
        _onDebug?.call('Verify result: ${result == null ? "null" : result}');
        // If verification returned null (e.g. prover sent JSON instead of binary proof),
        // try to get result from prover as JSON (same as Python binding fallback behaviour).
        if (result == null && provers.isNotEmpty) {
          _onDebug?.call('Verify was null, trying prover as JSON…');
          try {
            result = await _fetchRpc(provers, method, params, asProof: false);
            _onDebug?.call('Prover JSON result: $result');
          } catch (e) {
            _onDebug?.call('Prover JSON failed: $e');
          }
        }
        return result;
      case MethodType.unproofable:
        return _fetchRpc(ethRpcs, method, params, asProof: false);
      case MethodType.local:
        return verifyProof(Uint8List(0), method, params);
      case MethodType.notSupported:
      case MethodType.undefined:
        throw ColibriError('Method $method is not supported');
    }
  }

  /// Resolve proof via prover endpoints, falling back to local proof creation.
  Future<Uint8List> _fetchProofWithFallback(String method, List<dynamic> params) async {
    if (provers.isNotEmpty) {
      try {
        _onDebug?.call('Requesting proof from prover (zk_proof=$zkProof, ${provers.length} URL(s))…');
        final proof = await _fetchRpc(provers, method, params, asProof: true) as Uint8List;
        _onDebug?.call('Prover returned ${proof.length} bytes');
        return proof;
      } catch (e) {
        _onDebug?.call('Prover failed: $e → creating proof locally');
        return createProof(method, params);
      }
    }
    _onDebug?.call('No provers, creating proof locally');
    return createProof(method, params);
  }

  /// Handle pending native requests by fetching required data in parallel.
  Future<void> _handleRequests(
    List<DataRequest> requests, {
    required bool useProverFallback,
  }) async {
    Future<void> handleRequest(DataRequest request) async {
      try {
        final response = await _executeHttpRequest(request, useProverFallback: useProverFallback);
        _native.reqSetResponse(request.reqPtr, response.data, response.nodeIndex);
      } catch (error) {
        _native.reqSetError(request.reqPtr, error.toString(), 0);
      }
    }

    await Future.wait(requests.map(handleRequest));
  }

  /// Execute a single pending request against the configured endpoints.
  Future<_HttpResult> _executeHttpRequest(
    DataRequest request, {
    required bool useProverFallback,
  }) async {
    final servers = _selectServers(request, useProverFallback: useProverFallback);
    if (servers.isEmpty) {
      throw HTTPError('No servers configured for request type ${request.requestType}');
    }

    for (var i = 0; i < servers.length; i++) {
      if (request.excludeMask & (1 << i) != 0) {
        continue;
      }

      final url = request.url.isEmpty
          ? servers[i]
          : '${servers[i].replaceAll(RegExp(r"/+$"), "")}/${request.url.replaceAll(RegExp(r"^/+"), "")}';

      try {
        final response = await _sendHttp(request, url);
        if (response.statusCode == 200) {
          return _HttpResult(response.bodyBytes, i);
        }
      } catch (_) {
        continue;
      }
    }

    throw HTTPError('All servers failed for ${request.url}');
  }

  /// Select endpoint list for the request type.
  List<String> _selectServers(DataRequest request, {required bool useProverFallback}) {
    switch (request.requestType) {
      case 'checkpointz':
        return checkpointz;
      case 'beacon_api':
        if (useProverFallback && provers.isNotEmpty) {
          return provers;
        }
        return beaconApis;
      case 'prover':
        return provers;
      default:
        return ethRpcs;
    }
  }

  /// Send a single HTTP request for a pending data request.
  Future<http.Response> _sendHttp(DataRequest request, String url) async {
    final headers = <String, String>{
      'Accept': request.encoding == 'ssz' ? 'application/octet-stream' : 'application/json',
    };

    if (request.payload != null) {
      headers['Content-Type'] = 'application/json';
    }

    final method = request.method.toUpperCase();
    final uri = Uri.parse(url);

    final body = request.payload == null ? null : jsonEncode(request.payload);

    switch (method) {
      case 'GET':
        return _http.get(uri, headers: headers).timeout(const Duration(seconds: 30));
      case 'POST':
        return _http.post(uri, headers: headers, body: body).timeout(const Duration(seconds: 30));
      case 'PUT':
        return _http.put(uri, headers: headers, body: body).timeout(const Duration(seconds: 30));
      case 'DELETE':
        return _http.delete(uri, headers: headers, body: body).timeout(const Duration(seconds: 30));
      default:
        return _http.post(uri, headers: headers, body: body).timeout(const Duration(seconds: 30));
    }
  }

  /// Execute a direct RPC call (used for unproofable methods or prover requests).
  Future<dynamic> _fetchRpc(
    List<String> urls,
    String method,
    List<dynamic> params, {
    required bool asProof,
  }) async {
    final payload = <String, dynamic>{
      'id': 1,
      'jsonrpc': '2.0',
      'method': method,
      'params': params,
    };
    if (asProof) {
      payload['include_code'] = includeCode;
      payload['zk_proof'] = zkProof;
      payload['signers'] = checkpointWitnessKeys ?? '0x';
      final clientState = _clientStateHex();
      if (clientState != null) {
        payload['c4'] = clientState;
      }
      if (logProverRequests) {
        final hasState = clientState != null;
        final signers = checkpointWitnessKeys ?? '0x';
        print(
          'prover request: method=$method zk_proof=$zkProof '
          'include_code=$includeCode signers=$signers '
          'client_state=${hasState ? "set" : "none"}',
        );
      }
    }

    final headers = <String, String>{
      'Content-Type': 'application/json',
      'Accept': asProof ? 'application/octet-stream' : 'application/json',
    };

    for (final url in urls) {
      try {
        final response = await _http
            .post(Uri.parse(url), headers: headers, body: jsonEncode(payload))
            .timeout(const Duration(seconds: 30));

        if (response.statusCode == 200) {
          if (asProof) {
            final bodyBytes = response.bodyBytes;
            final len = bodyBytes.length;
            _onDebug?.call('prover response from $url: length=$len (ZK expects 260 bytes)');
            final contentType = (response.headers['content-type'] ?? response.headers['Content-Type'] ?? '').toString().toLowerCase();
            if (contentType.contains('application/json')) {
              _onDebug?.call('warning: Content-Type is application/json – response may be error/JSON, not binary proof');
            }
            if (len > 0 && bodyBytes[0] == 0x7b) {
              _onDebug?.call('warning: response starts with "{" (looks like JSON), proof may be wrong');
            }
            return Uint8List.fromList(bodyBytes);
          }
          final body = jsonDecode(response.body) as Map<String, dynamic>;
          if (body.containsKey('error')) {
            final error = body['error'] as Map<String, dynamic>;
            throw RPCError(
              error['message']?.toString() ?? 'RPC error',
              code: error['code'] is num ? (error['code'] as num).toInt() : null,
            );
          }
          return body['result'];
        }
      } catch (_) {
        continue;
      }
    }

    throw RPCError('All RPC servers failed for $method');
  }

  /// Ensure we have a trusted checkpoint before proof verification.
  Future<void> _ensureTrustedCheckpoint() async {
    if (_checkpointInitialized) {
      return;
    }
    _checkpointInitialized = true;

    if (zkProof) {
      return;
    }
    if (_runtimeTrustedCheckpoint != null || trustedCheckpoint != null) {
      return;
    }
    if (_clientStateHex() != null) {
      return;
    }

    final endpoints = <String>[
      ...checkpointz,
      ...beaconApis,
      ...provers,
    ];
    for (final base in endpoints) {
      try {
        final url = base.endsWith('/')
            ? '${base}eth/v1/beacon/states/head/finality_checkpoints'
            : '$base/eth/v1/beacon/states/head/finality_checkpoints';
        final response = await _http
            .get(Uri.parse(url), headers: {'Content-Type': 'application/json'})
            .timeout(const Duration(seconds: 30));
        if (response.statusCode != 200) {
          continue;
        }
        final body = jsonDecode(response.body) as Map<String, dynamic>;
        final data = body['data'];
        final finalized = data is Map<String, dynamic> ? data['finalized'] : null;
        final root = finalized is Map<String, dynamic> ? finalized['root'] : null;
        if (root is String && root.startsWith('0x') && root.length == 66) {
          _runtimeTrustedCheckpoint = root;
          return;
        }
      } catch (_) {
        continue;
      }
    }
  }

  /// Read the client state from storage as hex string for prover requests.
  String? _clientStateHex() {
    final storageInstance = storage;
    if (storageInstance == null) {
      return null;
    }
    final state = storageInstance.get('states_$chainId');
    if (state == null || state.isEmpty) {
      return null;
    }
    final buffer = StringBuffer('0x');
    for (final byte in state) {
      buffer.write(byte.toRadixString(16).padLeft(2, '0'));
    }
    return buffer.toString();
  }
}

/// HTTP response wrapper with node index for exclusion masks.
class _HttpResult {
  _HttpResult(this.data, this.nodeIndex);

  final Uint8List data;
  final int nodeIndex;
}

/// Default prover endpoints by chain.
List<String> _defaultProvers(int chainId) {
  return switch (chainId) {
    1 => ['https://mainnet1.colibri-proof.tech'],
    11155111 => ['https://sepolia.colibri-proof.tech'],
    100 => ['https://gnosis.colibri-proof.tech'],
    10200 => ['https://chiado.colibri-proof.tech'],
    _ => ['https://c4.incubed.net'],
  };
}

/// Default RPC endpoints by chain.
List<String> _defaultEthRpcs(int chainId) {
  return switch (chainId) {
    1 => ['https://rpc.ankr.com/eth'],
    11155111 => ['https://ethereum-sepolia-rpc.publicnode.com'],
    100 => ['https://rpc.ankr.com/gnosis'],
    10200 => ['https://gnosis-chiado-rpc.publicnode.com'],
    _ => ['https://rpc.ankr.com/eth'],
  };
}

/// Default beacon API endpoints by chain.
List<String> _defaultBeaconApis(int chainId) {
  return switch (chainId) {
    1 => ['https://lodestar-mainnet.chainsafe.io'],
    11155111 => ['https://ethereum-sepolia-beacon-api.publicnode.com'],
    100 => ['https://gnosis.colibri-proof.tech'],
    10200 => ['https://gnosis-chiado-beacon-api.publicnode.com'],
    _ => ['https://lodestar-mainnet.chainsafe.io'],
  };
}

/// Default checkpointz endpoints by chain.
List<String> _defaultCheckpointz(int chainId) {
  return switch (chainId) {
    1 => [
        'https://sync-mainnet.beaconcha.in',
        'https://beaconstate.info',
        'https://sync.invis.tools',
        'https://beaconstate.ethstaker.cc',
      ],
    _ => <String>[],
  };
}
