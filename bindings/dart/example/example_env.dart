/// Import file system utilities to detect .env presence.
import 'dart:io';

/// In-memory store for values loaded from `.env`.
final Map<String, String> _dotenvValues = {};

/// Load `.env` from the current working directory when present.
void loadExampleEnv() {
  /// Look for a `.env` file next to the `dart run` working directory.
  final file = File('.env');
  if (!file.existsSync()) {
    return;
  }
  /// Load environment variables from the file.
  final lines = file.readAsLinesSync();
  for (final raw in lines) {
    var line = raw.trim();
    if (line.isEmpty || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('export ')) {
      line = line.substring('export '.length).trim();
    }
    final equalsIndex = line.indexOf('=');
    if (equalsIndex <= 0) {
      continue;
    }
    final key = line.substring(0, equalsIndex).trim();
    var value = line.substring(equalsIndex + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith("'") && value.endsWith("'"))) {
      value = value.substring(1, value.length - 1);
    }
    _dotenvValues[key] = value;
  }
}

/// Read a key from `.env` first, then from the process environment.
String? readEnv(String key) {
  return _dotenvValues[key] ?? Platform.environment[key];
}

/// Parse a comma-separated environment variable into a list.
List<String>? _readCsvEnv(String key) {
  final raw = readEnv(key);
  if (raw == null || raw.trim().isEmpty) {
    return null;
  }
  final parts = raw.split(',');
  final cleaned = <String>[];
  for (final part in parts) {
    final value = part.trim();
    if (value.isNotEmpty) {
      cleaned.add(value);
    }
  }
  return cleaned.isEmpty ? null : cleaned;
}

/// Resolve prover endpoints from `COLIBRI_PROVER(S)`.
List<String>? resolveProvers() {
  return _readCsvEnv('COLIBRI_PROVERS') ?? _readCsvEnv('COLIBRI_PROVER');
}

/// Default public Ethereum RPC endpoints for examples.
List<String> defaultPublicEthRpcs() {
  return [
    'https://ethereum.publicnode.com',
    'https://rpc.flashbots.net',
    'https://cloudflare-eth.com',
  ];
}

/// Resolve Ethereum RPC endpoints from `COLIBRI_ETH_RPC(S)`.
List<String>? resolveEthRpcs({List<String>? fallback}) {
  return _readCsvEnv('COLIBRI_ETH_RPCS') ?? _readCsvEnv('COLIBRI_ETH_RPC') ?? fallback;
}

/// Parse a boolean environment flag.
bool _readBoolEnv(String key, {bool defaultValue = false}) {
  final raw = readEnv(key);
  if (raw == null || raw.trim().isEmpty) {
    return defaultValue;
  }
  final normalized = raw.trim().toLowerCase();
  return normalized == '1' || normalized == 'true' || normalized == 'yes' || normalized == 'on';
}

/// Resolve whether to request ZK sync proofs from the prover.
bool resolveZkProof() {
  return _readBoolEnv('COLIBRI_ZK_PROOF');
}

/// Resolve optional checkpoint witness keys for ZK proofs.
String? resolveCheckpointWitnessKeys() {
  return readEnv('COLIBRI_CHECKPOINT_WITNESS_KEYS');
}

/// Format a block number as a decimal string when possible.
String formatBlockNumber(dynamic value) {
  if (value == null) {
    return 'null';
  }
  if (value is int) {
    return value.toString();
  }
  if (value is String && value.startsWith('0x')) {
    try {
      return int.parse(value.substring(2), radix: 16).toString();
    } catch (_) {
      return value;
    }
  }
  return value.toString();
}
