// Colibri client for web: delegates to the JS/WASM Colibri bridge.
//
// Requires the Colibri WASM bridge to be loaded before use (e.g. via
// colibri_flutter web assets or by including the bridge script in index.html).
// See README and colibri_flutter web setup.

import 'dart:convert';
import 'dart:js_util';
import 'dart:typed_data';

import 'package:web/web.dart' as web;

import 'client_defaults.dart';
import 'storage.dart';
import 'types.dart';

/// High-level Colibri client for web (JS/WASM implementation).
///
/// Uses the same API as the native client. The JavaScript bridge (ColibriWebBridge)
/// must be loaded so that the WASM module is available (e.g. via colibri_flutter
/// web assets or a script tag in index.html).
class Colibri {
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
    dynamic httpClient,
  })  : provers = provers ?? defaultProvers(chainId),
        _onDebug = onDebug,
        ethRpcs = ethRpcs ?? defaultEthRpcs(chainId),
        beaconApis = beaconApis ?? defaultBeaconApis(chainId),
        checkpointz = checkpointz ?? defaultCheckpointz(chainId) {
    if (libraryPath != null && libraryPath.toString().isNotEmpty) {
      _wasmBaseUrl = libraryPath.toString();
    }
    _client = _createJsClient();
  }

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
  String? _wasmBaseUrl;
  late final Object _client;

  int _getVerifyFlags() => privacyMode == PrivacyMode.basic ? 2 : 0;

  Object _createJsClient() {
    final bridge = _getBridge();
    final config = _configToJs();
    final client = callMethod(bridge, 'createClient', [config]);
    if (client == null) {
      throw ColibriError(
        'ColibriWebBridge.createClient failed. '
        'Ensure the Colibri WASM bridge script is loaded (e.g. colibri_flutter web assets).',
      );
    }
    return client;
  }

  Object _getBridge() {
    final bridge = getProperty(web.window, 'colibriWebBridge');
    if (bridge == null) {
      throw ColibriError(
        'colibriWebBridge not found on window. '
        'Load the Colibri WASM bridge script before using Colibri on web (e.g. in index.html or via colibri_flutter).',
      );
    }
    return bridge;
  }

  Object _configToJs() {
    final map = <String, dynamic>{
      'chainId': chainId,
      'provers': provers,
      'ethRpcs': ethRpcs,
      'beaconApis': beaconApis,
      'checkpointz': checkpointz,
      'includeCode': includeCode,
      'useAccesslist': useAccesslist,
      'zkProof': zkProof,
      'verifyFlags': _getVerifyFlags(),
    };
    if (trustedCheckpoint != null) {
      map['trustedCheckpoint'] = trustedCheckpoint;
    }
    if (checkpointWitnessKeys != null) {
      map['checkpointWitnessKeys'] = checkpointWitnessKeys;
    }
    if (_wasmBaseUrl != null) {
      map['wasmBaseUrl'] = _wasmBaseUrl;
    }
    return jsify(map) ?? map;
  }

  void close() {
    try {
      callMethod(_client, 'close', []);
    } catch (_) {}
  }

  /// On web, method support is determined asynchronously by the JS client.
  /// This returns [MethodType.proofable] as a safe default; [rpc] will still work correctly.
  MethodType getMethodSupport(String method, {List<dynamic>? params}) {
    return MethodType.proofable;
  }

  Future<Uint8List> createProof(String method, List<dynamic> params) async {
    _onDebug?.call('createProof: method=$method');
    final promise = callMethod(_client, 'createProof', [method, params]);
    final result = await promiseToFuture<Object?>(promise);
    if (result == null) {
      throw ProofError('createProof returned null');
    }
    final resultMap = dartify(result) as Map<dynamic, dynamic>?;
    if (resultMap == null) {
      throw ProofError('createProof returned invalid result');
    }
    if (resultMap['error'] != null) {
      throw ProofError(resultMap['error'].toString());
    }
    final base64 = resultMap['proofBase64']?.toString();
    if (base64 == null || base64.isEmpty) {
      throw ProofError('createProof did not return proof');
    }
    return Uint8List.fromList(base64Decode(base64));
  }

  Future<dynamic> verifyProof(
    Uint8List proof,
    String method,
    List<dynamic> params,
  ) async {
    _onDebug?.call('verifyProof: method=$method');
    final proofBase64 = base64Encode(proof);
    final promise = callMethod(_client, 'verifyProof', [method, params, proofBase64]);
    final result = await promiseToFuture<Object?>(promise);
    if (result == null) {
      throw VerificationError('verifyProof returned null');
    }
    final resultMap = dartify(result) as Map<dynamic, dynamic>?;
    if (resultMap == null) {
      throw VerificationError('verifyProof returned invalid result');
    }
    if (resultMap['error'] != null) {
      throw VerificationError(resultMap['error'].toString());
    }
    return resultMap['result'];
  }

  Future<dynamic> rpc(String method, List<dynamic> params) async {
    _onDebug?.call('rpc: method=$method chainId=$chainId');
    final promise = callMethod(_client, 'rpc', [method, params]);
    final result = await promiseToFuture<Object?>(promise);
    if (result == null) {
      throw ColibriError('rpc returned null');
    }
    final resultMap = dartify(result) as Map<dynamic, dynamic>?;
    if (resultMap == null) {
      throw ColibriError('rpc returned invalid result');
    }
    if (resultMap['error'] != null) {
      throw ColibriError(resultMap['error'].toString());
    }
    return resultMap['result'];
  }
}
