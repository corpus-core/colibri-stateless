/// Import platform detection for selecting the correct native library file.
import 'dart:io';

/// Import the public Colibri API (client + types).
import 'package:colibri_stateless/colibri.dart';

/// Import helper utilities for loading .env configuration.
import 'example_env.dart';

/// Resolve the platform-specific library path for Colibri.
String _libraryPath() {
  /// Choose the correct shared library based on the OS.
  /// This mirrors the naming that CMake produces for the Dart binding.
  if (Platform.isWindows) {
    return 'native/colibri.dll';
  }
  if (Platform.isMacOS) {
    return 'native/libcolibri.dylib';
  }
  return 'native/libcolibri.so';
}

/// Example: manual proof creation and verification.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs();
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();
  final zkDebug = resolveZkDebug();

  /// Instantiate a client for mainnet and load the native library.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
    logProverRequests: zkDebug,
  );

  /// Pick a proofable method and its parameters.
  final method = 'eth_getBalance';
  final params = [
    '0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5',
    'latest',
  ];

  /// Step 1: create the proof locally using the native prover state machine.
  final proof = await colibri.createProof(method, params);

  /// Step 2: verify the proof and extract the verified RPC result.
  final verified = await colibri.verifyProof(proof, method, params);

  /// Print the verified value as a decimal string.
  print('Verified balance (dec): ${formatBlockNumber(verified)}');

  /// Release resources.
  colibri.close();
}
