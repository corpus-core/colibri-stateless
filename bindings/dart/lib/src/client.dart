import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import 'native.dart';
import 'storage.dart';
import 'types.dart';

class Colibri {
  Colibri({
    this.chainId = 1,
    List<String>? provers,
    List<String>? ethRpcs,
    List<String>? beaconApis,
    List<String>? checkpointz,
    this.trustedCheckpoint,
    this.includeCode = false,
    this.storage,
    String? libraryPath,
    http.Client? httpClient,
  })  : provers = provers ?? _defaultProvers(chainId),
        ethRpcs = ethRpcs ?? _defaultEthRpcs(chainId),
        beaconApis = beaconApis ?? _defaultBeaconApis(chainId),
        checkpointz = checkpointz ?? _defaultCheckpointz(chainId),
        _native = ColibriNative.load(libraryPath: libraryPath),
        _http = httpClient ?? http.Client() {
    final storageInstance = storage;
    if (storageInstance != null) {
      _native.registerStorage(storageInstance);
    }
  }

  final int chainId;
  final List<String> provers;
  final List<String> ethRpcs;
  final List<String> beaconApis;
  final List<String> checkpointz;
  final String? trustedCheckpoint;
  final bool includeCode;
  final ColibriStorage? storage;

  final ColibriNative _native;
  final http.Client _http;

  void close() {
    _http.close();
  }

  MethodType getMethodSupport(String method) {
    final support = _native.getMethodSupport(chainId, method);
    return MethodType.fromValue(support);
  }

  Future<Uint8List> createProof(String method, List<dynamic> params) async {
    final paramsJson = jsonEncode(params);
    final ctx = _native.createProverCtx(
      method,
      paramsJson,
      chainId,
      includeCode ? 1 : 0,
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
    final checkpoint = trustedCheckpoint ?? '';
    final ctx = _native.verifyCreateCtx(
      proof,
      method,
      paramsJson,
      chainId,
      checkpoint,
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
    final support = getMethodSupport(method);

    switch (support) {
      case MethodType.proofable:
        final proof = await _fetchProofWithFallback(method, params);
        return verifyProof(proof, method, params);
      case MethodType.unproofable:
        return _fetchRpc(ethRpcs, method, params, asProof: false);
      case MethodType.local:
        return verifyProof(Uint8List(0), method, params);
      case MethodType.notSupported:
      case MethodType.undefined:
        throw ColibriError('Method $method is not supported');
    }
  }

  Future<Uint8List> _fetchProofWithFallback(String method, List<dynamic> params) async {
    if (provers.isNotEmpty) {
      try {
        return await _fetchRpc(provers, method, params, asProof: true) as Uint8List;
      } catch (_) {
        return createProof(method, params);
      }
    }
    return createProof(method, params);
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
          return provers;
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
            return Uint8List.fromList(response.bodyBytes);
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
}

class _HttpResult {
  _HttpResult(this.data, this.nodeIndex);

  final Uint8List data;
  final int nodeIndex;
}

List<String> _defaultProvers(int chainId) {
  return switch (chainId) {
    1 => ['https://mainnet1.colibri-proof.tech'],
    11155111 => ['https://sepolia.colibri-proof.tech'],
    100 => ['https://gnosis.colibri-proof.tech'],
    10200 => ['https://chiado.colibri-proof.tech'],
    _ => ['https://c4.incubed.net'],
  };
}

List<String> _defaultEthRpcs(int chainId) {
  return switch (chainId) {
    1 => ['https://rpc.ankr.com/eth'],
    11155111 => ['https://ethereum-sepolia-rpc.publicnode.com'],
    100 => ['https://rpc.ankr.com/gnosis'],
    10200 => ['https://gnosis-chiado-rpc.publicnode.com'],
    _ => ['https://rpc.ankr.com/eth'],
  };
}

List<String> _defaultBeaconApis(int chainId) {
  return switch (chainId) {
    1 => ['https://lodestar-mainnet.chainsafe.io'],
    11155111 => ['https://ethereum-sepolia-beacon-api.publicnode.com'],
    100 => ['https://gnosis.colibri-proof.tech'],
    10200 => ['https://gnosis-chiado-beacon-api.publicnode.com'],
    _ => ['https://lodestar-mainnet.chainsafe.io'],
  };
}

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
