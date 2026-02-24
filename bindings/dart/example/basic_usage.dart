/// Import platform detection for selecting the correct native library file.
import 'dart:io';

/// Import the public Colibri API (client + types).
import 'package:colibri_stateless/colibri.dart';

/// Import helper utilities for loading .env configuration.
import 'example_env.dart';

/// Minimal example: make a verified RPC call and print the result.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();

  /// Resolve the platform-specific path to the native Colibri library.
  /// This matches the output location of `./build.sh` in bindings/dart/native.
  final libraryPath = Platform.isWindows
      ? 'native/colibri.dll'
      : Platform.isMacOS
          ? 'native/libcolibri.dylib'
          : 'native/libcolibri.so';

  /// Create the Colibri client for Ethereum mainnet (chainId 1).
  /// The client loads the native library and prepares HTTP transport.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: libraryPath,
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
  );

  /// Call a simple RPC method.
  /// Colibri decides whether to prove (proofable) or call directly.
  final blockNumber = await colibri.rpc('eth_blockNumber', []);

  /// Print the verified block number as a decimal string.
  print('Block number (dec): ${formatBlockNumber(blockNumber)}');

  /// Always close the client to release HTTP resources.
  colibri.close();
}
