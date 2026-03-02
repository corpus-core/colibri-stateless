import 'dart:io';
import 'dart:typed_data';

import 'package:colibri_stateless/colibri.dart';

import 'example_env.dart';

/// Formats up to [len] bytes of [proof] starting at [offset] as hex.
String _proofHex(Uint8List proof, int offset, int len) {
  if (proof.isEmpty) return '(empty)';
  final start = offset.clamp(0, proof.length);
  final end = (start + len).clamp(0, proof.length);
  if (start >= end) return '(out of range)';
  return proof.sublist(start, end).map((b) => b.toRadixString(16).padLeft(2, '0')).join(' ');
}

/// Returns the platform-specific path to the native Colibri library.
String _libraryPath() {
  if (Platform.isWindows) return 'native/colibri.dll';
  if (Platform.isMacOS) return 'native/libcolibri.dylib';
  return 'native/libcolibri.so';
}

/// Example: manual proof creation and verification.
///
/// Creates a proof for `eth_getBalance`, then verifies it locally
/// and prints the verified balance.
Future<void> main() async {
  loadExampleEnv();

  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs();
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();
  final zkDebug = resolveZkDebug();

  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
    logProverRequests: zkDebug,
  );

  final method = 'eth_getBalance';
  final params = ['0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5', 'latest'];

  Uint8List proof;
  try {
    // Step 1: create the proof (from prover if available, otherwise locally).
    proof = await colibri.createProof(method, params);
  } on ColibriError catch (e) {
    // Local proof creation can fail if required data (e.g. period_store) cannot be
    // fetched from the configured prover/beacon URLs. Use rpc() for automatic
    // prover-first flow, or ensure your .env provers/beacon_apis serve the needed resources.
    print('Proof creation failed: ${e.message}');
    print('Tip: Run basic_usage.dart for prover-backed calls, or check prover/beacon URLs.');
    colibri.close();
    exitCode = 1;
    return;
  }

  print('Proof length: ${proof.length} bytes');
  print('Proof (first 64 bytes hex): ${_proofHex(proof, 0, 64)}');
  print('Proof (last 32 bytes hex): ${_proofHex(proof, proof.length - 32, 32)}');

  // Step 2: verify the proof and extract the result.
  final verified = await colibri.verifyProof(proof, method, params);
  print('Verified balance (dec): ${formatBlockNumber(verified)}');

  colibri.close();
}
