/// Import platform detection for selecting the correct native library file.
import 'dart:io';

/// Import the public Colibri API (client + types).
import 'package:colibri_stateless/colibri.dart';

/// Import helper utilities for loading .env configuration.
import 'example_env.dart';

/// Resolve the platform-specific library path for Colibri.
String _libraryPath() {
  /// Resolve native library path for the current OS.
  if (Platform.isWindows) {
    return 'native/colibri.dll';
  }
  if (Platform.isMacOS) {
    return 'native/libcolibri.dylib';
  }
  return 'native/libcolibri.so';
}

/// Example: read a block by number and print basic fields.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();
  final zkDebug = resolveZkDebug();

  /// Create the client for Ethereum mainnet and load the native library.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
    logProverRequests: zkDebug,
  );

  /// Select a block number; "latest" requests the most recent block.
  final blockNumber = 'latest';

  /// Request the block without full transactions (faster, returns hashes).
  final block = await colibri.rpc('eth_getBlockByNumber', [blockNumber, false]);

  /// The result is a JSON-like map. Extract a few fields defensively.
  if (block is Map<String, dynamic>) {
    print('Block number (dec): ${formatBlockNumber(block['number'])}');
    print('Block hash: ${block['hash']}');
    print('Tx count: ${(block['transactions'] as List?)?.length ?? 0}');
  } else {
    /// If the result is not a map, just print it directly.
    print('Block result: $block');
  }

  /// Clean up HTTP resources.
  colibri.close();
}
