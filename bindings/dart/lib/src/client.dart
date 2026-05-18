import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
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
class Colibri {
  /// Creates a client with optional custom endpoints and storage.
  ///
  /// [libraryPath] overrides the platform default native library location.
  /// [storage] registers Dart callbacks for the native cache layer.
  /// [zkProof] requests ZK sync proofs from remote provers when available.
  /// [checkpointWitnessKeys] provides signer keys for ZK proof verification.
  /// [onDebug] if set, called with short messages during rpc/verify (e.g. for UI log).
  /// Can contain sensitive data (e.g. witness keys, params). Do not forward to
  /// production logging without redaction.
  ///
  /// On Android and iOS, if [storage] is not provided, [MemoryStorage] is used
  /// by default so the native cache works (the C-side file storage cannot write
  /// on mobile). On desktop, omitting [storage] uses the native file storage.
  Colibri({
    this.chainId = 1,
    List<String>? provers,
    List<String>? ethRpcs,
    List<String>? beaconApis,
    List<String>? checkpointz,
    List<String>? obliviousNodes,
    this.trustedCheckpoint,
    this.includeCode = false,
    this.useAccesslist = false,
    this.zkProof = false,
    this.privacyMode = PrivacyMode.none,
    this.proverMode,
    this.checkpointWitnessKeys,
    this.logProverRequests = false,
    this.storage,
    this.rpcTimeout = const Duration(seconds: 30),
    this.proverTimeout = const Duration(seconds: 120),
    void Function(String message)? onDebug,
    String? libraryPath,
    http.Client? httpClient,
  })  : provers = provers ?? _defaultProvers(chainId),
        _onDebug = onDebug,
        ethRpcs = ethRpcs ?? _defaultEthRpcs(chainId),
        beaconApis = beaconApis ?? _defaultBeaconApis(chainId),
        checkpointz = checkpointz ?? _defaultCheckpointz(chainId),
        obliviousNodes = obliviousNodes ?? const [],
        _native = ColibriNative.load(libraryPath: libraryPath),
        _http = httpClient ?? http.Client() {
    _runtimeTrustedCheckpoint = trustedCheckpoint;
    _effectiveStorage = storage ?? ((Platform.isAndroid || Platform.isIOS) ? MemoryStorage() : null);
    if (_effectiveStorage != null) {
      _native.registerStorage(_effectiveStorage!);
    }
  }

  /// Storage actually used (user-provided or default [MemoryStorage] on Android/iOS).
  ColibriStorage? _effectiveStorage;

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
  /// TEE RPC endpoints for eth_getProof (privacy-preserving storage reads).
  final List<String> obliviousNodes;
  /// Optional trusted checkpoint root (0x-prefixed hex).
  final String? trustedCheckpoint;
  /// Whether to include code in proof requests.
  final bool includeCode;
  /// Whether to include an access list in proof requests.
  final bool useAccesslist;
  /// Whether to request ZK sync proofs from provers.
  final bool zkProof;
  /// PAP mode; [PrivacyMode.basic] sets VERIFY_FLAG_PAP. Also set automatically when [obliviousNodes] is non-empty.
  final PrivacyMode privacyMode;
  /// Proof generation mode. null = auto-detect (remote if provers configured, else local).
  final ProverMode? proverMode;
  /// Optional witness signer keys for ZK proof verification.
  final String? checkpointWitnessKeys;
  /// Whether to log prover request parameters (debug only). When true, only
  /// non-sensitive summaries are printed; do not enable in production.
  final bool logProverRequests;
  /// Optional storage backend for native cache.
  final ColibriStorage? storage;
  /// Timeout for direct RPC calls (eth_blockNumber, eth_call, …).
  final Duration rpcTimeout;
  /// Timeout for prover requests (proof fetching, which can involve many
  /// internal beacon calls and take significantly longer).
  final Duration proverTimeout;

  final void Function(String message)? _onDebug;
  final ColibriNative _native;
  final http.Client _http;
  String? _runtimeTrustedCheckpoint;
  Timer? _lightClientTimer;

  /// Closes the underlying HTTP client and stops any light client polling.
  ///
  /// Call when the client is no longer needed to release resources.
  void close() {
    stopLightClient();
    _http.close();
  }

  /// Starts background polling to keep the block header cache warm.
  /// Useful for [ProverMode.lightClient].
  ///
  /// By default polls `eth_getBlockHeader("latest")` which fetches only the
  /// compact header proof. Set [fullBlock] to `true` to poll
  /// `eth_getBlockByNumber("latest")` instead -- useful when many
  /// `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow,
  /// since those need the full block data.
  ///
  /// [interval] defaults to 12 seconds (one Ethereum slot).
  void startLightClient({Duration interval = const Duration(seconds: 12), bool fullBlock = false}) {
    stopLightClient();
    final method = fullBlock ? 'eth_getBlockByNumber' : 'eth_getBlockHeader';
    final params = fullBlock ? ['latest', false] : ['latest'];
    _lightClientTimer = Timer.periodic(interval, (_) async {
      try {
        await rpc(method, params);
      } catch (e) {
        _onDebug?.call('lightClient poll error: $e');
      }
    });
    _onDebug?.call('lightClient started (interval=${interval.inSeconds}s, fullBlock=$fullBlock)');
  }

  /// Stops background light client polling started by [startLightClient].
  void stopLightClient() {
    _lightClientTimer?.cancel();
    _lightClientTimer = null;
  }

  /// Returns verify flags derived from [privacyMode] and [obliviousNodes].
  /// PAP is enabled when [privacyMode] is basic or [obliviousNodes] is set (oblivious requires PAP).
  int _getVerifyFlags() {
    final pap = privacyMode == PrivacyMode.basic || obliviousNodes.isNotEmpty;
    return (pap ? 2 : 0) | (obliviousNodes.isNotEmpty ? (1 << 6) : 0);
  }

  /// Returns how [method] is supported (proofable, local, unproofable, etc.).
  ///
  /// In PAP mode the result may depend on cached data for [params].
  MethodType getMethodSupport(String method, {List<dynamic>? params}) {
    final paramsJson = params != null ? jsonEncode(params) : null;
    final support = _native.getMethodSupport(
      chainId,
      method,
      params: paramsJson,
      flags: _getVerifyFlags(),
    );
    return MethodType.fromValue(support);
  }

  /// Creates a proof for [method] and [params] locally using the native library.
  ///
  /// Returns the serialized proof bytes. Use [verifyProof] to verify and get
  /// the result. Throws [ProofError] on failure.
  /// Local proof creation always uses Merkle proofs; [zkProof] is ignored here
  /// (ZK proofs are produced by remote provers, not the local prover).
  Future<Uint8List> createProof(String method, List<dynamic> params) async {
    final paramsJson = jsonEncode(params);
    final flags = (includeCode ? 1 : 0) | (useAccesslist ? (1 << 6) : 0);
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
      'checkpoint=${checkpoint.isEmpty ? "(empty)" : "${checkpoint.length} chars"}',
    );
    final ctx = _native.verifyCreateCtx(
      proof,
      method,
      paramsJson,
      chainId,
      checkpoint,
      flags: _getVerifyFlags(),
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

  /// Executes an RPC call via the unified C core state machine.
  ///
  /// The C core handles method type detection, proof generation (local or
  /// remote), and verification internally. The host only handles pending
  /// data requests.
  Future<dynamic> rpc(String method, List<dynamic> params) async {
    final paramsJson = jsonEncode(params);
    final proverFlags = (includeCode ? 1 : 0) | (useAccesslist ? (1 << 6) : 0) | (zkProof ? (1 << 7) : 0);
    final resolvedMode = proverMode ?? (provers.isEmpty ? ProverMode.local : ProverMode.remote);
    final nativeMode = resolvedMode == ProverMode.lightClient ? ProverMode.hybrid.value : resolvedMode.value;

    _onDebug?.call('rpc: method=$method proverMode=$nativeMode chainId=$chainId');

    final ctx = _native.createRpcCtx(method, paramsJson, chainId, proverFlags, _getVerifyFlags(), nativeMode);
    if (ctx == ffi.nullptr) {
      throw ColibriError('Failed to create RPC context for $method');
    }

    if (resolvedMode == ProverMode.proxy) {
      _native.rpcSetProxyUrls(ctx, ethRpcs.join(','), beaconApis.join(','));
    }

    final checkpoint = _runtimeTrustedCheckpoint ?? trustedCheckpoint;
    if (checkpoint != null && checkpoint.isNotEmpty) {
      _native.setCheckpoint(chainId, checkpoint);
    }
    if (checkpointWitnessKeys != null && checkpointWitnessKeys!.isNotEmpty) {
      _native.rpcSetWitnessKeys(ctx, checkpointWitnessKeys!);
    }

    try {
      while (true) {
        final statusJson = _native.rpcExecuteJsonStatus(ctx);
        final status = jsonDecode(statusJson) as Map<String, dynamic>;

        switch (status['status']) {
          case 'success':
            _onDebug?.call('rpc: $method → success');
            return status['result'];
          case 'error':
            final errorMsg = status['error']?.toString() ?? 'Unknown RPC error';
            _onDebug?.call('rpc: $method → error: $errorMsg');
            throw ColibriError(errorMsg);
          case 'pending':
            final requests = (status['requests'] as List<dynamic>? ?? [])
                .whereType<Map<String, dynamic>>()
                .map(DataRequest.fromJson)
                .toList();
            _onDebug?.call('rpc: $method → pending (${requests.length} requests)');
            await _handleRequests(requests, useProverFallback: true);
            break;
          default:
            throw ColibriError('Unknown RPC status: ${status['status']}');
        }
      }
    } finally {
      _native.freeRpcCtx(ctx);
    }
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
          return [...provers, ...beaconApis];
        }
        return beaconApis;
      case 'prover':
        return provers;
      default:
        if (request.payload?['method'] == 'eth_getProof' && obliviousNodes.isNotEmpty) {
          return obliviousNodes;
        }
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
        return _http.get(uri, headers: headers).timeout(rpcTimeout);
      case 'POST':
        return _http.post(uri, headers: headers, body: body).timeout(rpcTimeout);
      case 'PUT':
        return _http.put(uri, headers: headers, body: body).timeout(rpcTimeout);
      case 'DELETE':
        return _http.delete(uri, headers: headers, body: body).timeout(rpcTimeout);
      default:
        return _http.post(uri, headers: headers, body: body).timeout(rpcTimeout);
    }
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
