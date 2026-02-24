/// Import platform detection for selecting the correct native library file.
import 'dart:io';
/// Byte storage container used by the storage interface.
import 'dart:typed_data';

/// Import the public Colibri API (client + storage abstraction).
import 'package:colibri_stateless/colibri.dart';

/// Import helper utilities for loading .env configuration.
import 'example_env.dart';

/// Resolve the platform-specific library path for Colibri.
String _libraryPath() {
  /// Resolve native library path per platform.
  if (Platform.isWindows) {
    return 'native/colibri.dll';
  }
  if (Platform.isMacOS) {
    return 'native/libcolibri.dylib';
  }
  return 'native/libcolibri.so';
}

/// A minimal custom storage implementation for Colibri's native cache.
class SimpleStorage implements ColibriStorage {
  /// In-memory cache for sync committee/state blobs.
  final Map<String, Uint8List> _cache = {};

  @override
  Uint8List? get(String key) => _cache[key];

  @override
  void set(String key, Uint8List value) {
    /// Copy to avoid accidental mutation of stored values.
    _cache[key] = Uint8List.fromList(value);
  }

  @override
  void delete(String key) {
    _cache.remove(key);
  }
}

/// Example: use a custom storage backend for cached sync state.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();

  /// Create a client with a custom storage backend.
  /// This storage is used by the native verifier for cached sync state.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    storage: SimpleStorage(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
  );

  /// Execute a verified RPC call.
  final result = await colibri.rpc('eth_blockNumber', []);
  print('Block number (dec): ${formatBlockNumber(result)}');

  /// Clean up HTTP resources.
  colibri.close();
}
