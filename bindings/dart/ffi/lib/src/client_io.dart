import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';

import 'package:colibri_stateless/src/client_defaults.dart';
import 'package:colibri_stateless/src/storage.dart';
import 'package:colibri_stateless/src/types.dart';
import 'package:http/http.dart' as http;

import 'native.dart';

/// High-level Colibri client for proofed RPC calls (native/FFI implementation).
///
/// Use [rpc] to run Ethereum (and compatible) RPC methods with automatic
/// proof generation and verification. Configure [provers], [ethRpcs], and
/// [beaconApis] for your network; optionally set [storage] for native cache.
class Colibri {
  /// Creates a client with optional custom endpoints and storage.
  Colibri({
    this.chainId = 1,
    List<String>? provers,
    List<String>? ethRpcs,
    List<String>? beaconApis,
    List<String>? checkpointz,
    this.trustedCheckpoint,
    this.includeCode = false,
    this.useAccesslist = false,
    this.zkProof = false,
    this.privacyMode = PrivacyMode.none,
    this.checkpointWitnessKeys,
    this.logProverRequests = false,
    this.storage,
    this.rpcTimeout = const Duration(seconds: 30),
    this.proverTimeout = const Duration(seconds: 120),
    void Function(String message)? onDebug,
    String? libraryPath,
    http.Client? httpClient,
  })  : provers = provers ?? defaultProvers(chainId),
        _onDebug = onDebug,
        ethRpcs = ethRpcs ?? defaultEthRpcs(chainId),
        beaconApis = beaconApis ?? defaultBeaconApis(chainId),
        checkpointz = checkpointz ?? defaultCheckpointz(chainId),
        _native = ColibriNative.load(libraryPath: libraryPath),
        _http = httpClient ?? http.Client() {
    _runtimeTrustedCheckpoint = trustedCheckpoint;
    _effectiveStorage = storage ?? ((Platform.isAndroid || Platform.isIOS) ? MemoryStorage() : null);
    if (_effectiveStorage != null) {
      _native.registerStorage(_effectiveStorage!);
    }
  }

  ColibriStorage? _effectiveStorage;

  final int chainId;
  final List<String> provers;
  final List<String> ethRpcs;
  final List<String> beaconApis;
  final List<String> checkpointz;
  final String? trustedCheckpoint;
  final bool includeCode;
  final bool useAccesslist;
  final bool zkProof;
  final PrivacyMode privacyMode;
  final String? checkpointWitnessKeys;
  final bool logProverRequests;
  final ColibriStorage? storage;
  final Duration rpcTimeout;
  final Duration proverTimeout;

  final void Function(String message)? _onDebug;
  final ColibriNative _native;
  final http.Client _http;
  String? _runtimeTrustedCheckpoint;

  void close() {
    _http.close();
  }

  int _getVerifyFlags() => privacyMode == PrivacyMode.basic ? 2 : 0;

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

  Future<dynamic> rpc(String method, List<dynamic> params) async {
    final paramsJson = jsonEncode(params);
    final proverFlags = (includeCode ? 1 : 0) | (useAccesslist ? (1 << 6) : 0) | (zkProof ? (1 << 7) : 0);
    final useRemote = provers.isEmpty ? 0 : 1;

    _onDebug?.call('rpc: method=$method useRemote=$useRemote chainId=$chainId');

    final ctx = _native.createRpcCtx(method, paramsJson, chainId, proverFlags, _getVerifyFlags(), useRemote);
    if (ctx == ffi.nullptr) {
      throw ColibriError('Failed to create RPC context for $method');
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
        return ethRpcs;
    }
  }

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

class _HttpResult {
  _HttpResult(this.data, this.nodeIndex);

  final Uint8List data;
  final int nodeIndex;
}
