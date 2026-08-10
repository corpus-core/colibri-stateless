import 'dart:io';

import 'package:colibri_stateless/colibri.dart';

import 'example_env.dart';

/// Minimal example: make a verified RPC call and print the result.
///
/// Loads configuration from `.env`, creates a [Colibri] client for
/// Ethereum mainnet, and calls `eth_blockNumber` with automatic proof
/// handling.
Future<void> main() async {
  loadExampleEnv();

  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();
  final zkDebug = resolveZkDebug();

  final libraryPath = Platform.isWindows
      ? 'native/colibri.dll'
      : Platform.isMacOS
          ? 'native/libcolibri.dylib'
          : 'native/libcolibri.so';

  final colibri = Colibri(
    chainId: 1,
    libraryPath: libraryPath,
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
    logProverRequests: zkDebug,
  );

  final blockNumber = await colibri.rpc('eth_blockNumber', []);
  print('Block number (dec): ${formatBlockNumber(blockNumber)}');

  colibri.close();
}
