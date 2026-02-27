import 'dart:io';

import 'package:flutter/services.dart';

/// In-memory store for values loaded from `.env` (asset or process env).
final Map<String, String> _dotenvValues = {};

/// Load `.env` from the app asset `assets/.env`, then overlay [Platform.environment].
/// Call once at startup (e.g. in main() before runApp).
Future<void> loadExampleEnv() async {
  try {
    final content = await rootBundle.loadString('assets/.env');
    for (final raw in content.split('\n')) {
      var line = raw.trim();
      if (line.isEmpty || line.startsWith('#')) continue;
      if (line.startsWith('export ')) line = line.substring(7).trim();
      final eq = line.indexOf('=');
      if (eq <= 0) continue;
      final key = line.substring(0, eq).trim();
      var value = line.substring(eq + 1).trim();
      if ((value.startsWith('"') && value.endsWith('"')) ||
          (value.startsWith("'") && value.endsWith("'"))) {
        value = value.substring(1, value.length - 1);
      }
      _dotenvValues[key] = value;
    }
  } catch (_) {
    // Asset missing or unreadable: use only process environment
  }
  // Overlay process environment so env vars set at run time win
  for (final e in Platform.environment.entries) {
    _dotenvValues[e.key] = e.value;
  }
}

/// Read a key from loaded .env first, then from process environment.
String? readEnv(String key) {
  return _dotenvValues[key] ?? Platform.environment[key];
}

List<String>? _readCsvEnv(String key) {
  final raw = readEnv(key);
  if (raw == null || raw.trim().isEmpty) return null;
  final cleaned = raw.split(',').map((e) => e.trim()).where((e) => e.isNotEmpty).toList();
  return cleaned.isEmpty ? null : cleaned;
}

/// Resolve prover endpoints from `COLIBRI_PROVER(S)`.
List<String>? resolveProvers() {
  return _readCsvEnv('COLIBRI_PROVERS') ?? _readCsvEnv('COLIBRI_PROVER');
}

/// Default public Ethereum RPC endpoints (same as Dart example).
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

bool _readBoolEnv(String key, {bool defaultValue = false}) {
  final raw = readEnv(key);
  if (raw == null || raw.trim().isEmpty) return defaultValue;
  final n = raw.trim().toLowerCase();
  return n == '1' || n == 'true' || n == 'yes' || n == 'on';
}

bool resolveZkProof() => _readBoolEnv('COLIBRI_ZK_PROOF');
String? resolveCheckpointWitnessKeys() => readEnv('COLIBRI_CHECKPOINT_WITNESS_KEYS');
bool resolveZkDebug() => _readBoolEnv('COLIBRI_DEBUG_ZK');

/// Format block number like Dart example (hex → decimal when possible).
String formatBlockNumber(dynamic value) {
  if (value == null) return 'null';
  if (value is int) return value.toString();
  if (value is String && value.startsWith('0x')) {
    try {
      return int.parse(value.substring(2), radix: 16).toString();
    } catch (_) {
      return value;
    }
  }
  return value.toString();
}
