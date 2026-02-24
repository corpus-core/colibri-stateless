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

/// Example: fetch and print a transaction receipt.
Future<void> main() async {
  /// Load optional configuration from a local .env file.
  loadExampleEnv();

  /// Resolve prover and RPC endpoints from environment variables if set.
  final provers = resolveProvers();
  final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs()) ?? defaultPublicEthRpcs();
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

  /// Replace with a known transaction hash you want to inspect.
  final txHash = '0xa613c8f3b835901d7c08a1eee3c227e58045e669a8b7a734bd0985ac53b28fd7';

  /// Request the receipt via a proofable method.
  dynamic receipt;
  try {
    receipt = await colibri.rpc('eth_getTransactionReceipt', [txHash]);
  } catch (_) {
    /// Fall back to a direct JSON-RPC call when proofing fails.
    receipt = await _fetchReceiptDirect(ethRpcs, txHash);
  }

  /// Print receipt details if available.
  if (receipt is Map<String, dynamic>) {
    print('Status: ${receipt['status']}');
    print('Block (dec): ${formatBlockNumber(receipt['blockNumber'])}');
    print('Gas used: ${receipt['gasUsed']}');
    print('Logs: ${(receipt['logs'] as List?)?.length ?? 0}');
  } else {
    print('Receipt result: $receipt');
  }

  /// Clean up HTTP resources.
  colibri.close();
}

/// Direct JSON-RPC call to fetch a receipt without proofing.
Future<dynamic> _fetchReceiptDirect(List<String> ethRpcs, String txHash) async {
  final body = jsonEncode({
    'jsonrpc': '2.0',
    'id': 1,
    'method': 'eth_getTransactionReceipt',
    'params': [txHash],
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
  throw Exception('All RPC endpoints failed for eth_getTransactionReceipt.');
}
