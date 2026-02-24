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

/// Example: call a smart contract with eth_call (read-only).
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();
  final zkDebug = resolveZkDebug();

  /// Create the client for Ethereum mainnet.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
    logProverRequests: zkDebug,
  );

  /// Call a token contract's `balanceOf(address)` (ERC-20).
  /// Function selector for balanceOf(address) is 0x70a08231.
  /// Append the 32-byte address parameter (left-padded).
  final target = '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48'; // USDC
  final account = '0x0000000000000000000000000000000000000000';
  final data = '0x70a08231'
      '000000000000000000000000'
      '${account.replaceFirst('0x', '')}';

  /// Build the eth_call payload.
  final call = {
    'to': target,
    'data': data,
  };

  /// Execute the call at the latest block.
  final result = await colibri.rpc('eth_call', [call, 'latest']);
  print('Raw balanceOf result: $result');

  /// Clean up HTTP resources.
  colibri.close();
}
