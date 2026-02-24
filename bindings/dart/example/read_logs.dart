/// Import JSON handling for a direct RPC fallback call.
import 'dart:convert';

/// Import platform detection for selecting the correct native library file.
import 'dart:io';

/// Import the public Colibri API (client + types).
import 'package:colibri_stateless/colibri.dart';

/// Import HTTP client for a direct RPC fallback call.
import 'package:http/http.dart' as http;

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

/// Example: read logs for a contract and block range.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs()) ?? defaultPublicEthRpcs();
  final zkProof = resolveZkProof();
  final checkpointWitnessKeys = resolveCheckpointWitnessKeys();

  /// Create the client for Ethereum mainnet.
  final colibri = Colibri(
    chainId: 1,
    libraryPath: _libraryPath(),
    provers: provers,
    ethRpcs: ethRpcs,
    zkProof: zkProof,
    checkpointWitnessKeys: checkpointWitnessKeys,
  );

  /// Configure a basic filter: contract address + block range.
  final filter = {
    'address': '0xdAC17F958D2ee523a2206206994597C13D831ec7', // USDT
    'fromBlock': 'latest',
    'toBlock': 'latest',
  };

  /// Request logs with the filter using Colibri proof flow.
  dynamic logs;
  try {
    logs = await colibri.rpc('eth_getLogs', [filter]);
  } catch (error) {
    /// If proofing fails (e.g., RPC data gaps), fall back to direct RPC.
    logs = await _fetchLogsDirect(ethRpcs, filter);
  }

  /// Print how many logs we received.
  if (logs is List) {
    print('Logs count: ${logs.length}');
    if (logs.isNotEmpty && logs.first is Map<String, dynamic>) {
      print('First log tx hash: ${(logs.first as Map<String, dynamic>)['transactionHash']}');
    }
  } else {
    print('Logs result: $logs');
  }

  /// Clean up HTTP resources.
  colibri.close();
}

/// Direct JSON-RPC call to fetch logs without proofing.
Future<dynamic> _fetchLogsDirect(List<String> ethRpcs, Map<String, dynamic> filter) async {
  final body = jsonEncode({
    'jsonrpc': '2.0',
    'id': 1,
    'method': 'eth_getLogs',
    'params': [filter],
  });
  for (final url in ethRpcs) {
    try {
      final response = await http.post(
        Uri.parse(url),
        headers: {'content-type': 'application/json'},
        body: body,
      );
      final payload = jsonDecode(response.body) as Map<String, dynamic>;
      if (payload.containsKey('result')) {
        return payload['result'];
      }
      if (payload.containsKey('error')) {
        continue;
      }
    } catch (_) {
      continue;
    }
  }
  throw Exception('All RPC endpoints failed for eth_getLogs.');
}
