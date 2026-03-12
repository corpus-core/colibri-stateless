import 'dart:io' show Platform;

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter/services.dart';

/// In-memory store for values loaded from `.env` (asset or process env).
final Map<String, String> _dotenvValues = {};

/// Load `.env` from the app asset `assets/.env`, then overlay process environment (VM only).
/// Call once at startup (e.g. in main() before runApp).
/// On web, [Platform.environment] is unavailable; only asset `.env` is used.
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
    // Asset missing or unreadable: use only process environment on VM
  }
  // Overlay process environment on VM/mobile/desktop (not available on web)
  if (!kIsWeb) {
    for (final e in Platform.environment.entries) {
      _dotenvValues[e.key] = e.value;
    }
  }
}

/// Read a key from loaded .env first, then from process environment (VM only).
String? readEnv(String key) {
  final fromDotenv = _dotenvValues[key];
  if (fromDotenv != null) return fromDotenv;
  if (kIsWeb) return null;
  return Platform.environment[key];
}

/// Splits a comma-separated env value into a trimmed list, or `null` if empty.
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

/// Reads [key] as a boolean (`true`, `1`, `yes`, `on`).
bool _readBoolEnv(String key, {bool defaultValue = false}) {
  final raw = readEnv(key);
  if (raw == null || raw.trim().isEmpty) return defaultValue;
  final n = raw.trim().toLowerCase();
  return n == '1' || n == 'true' || n == 'yes' || n == 'on';
}

/// Whether ZK sync proofs are enabled (`COLIBRI_ZK_PROOF`).
bool resolveZkProof() => _readBoolEnv('COLIBRI_ZK_PROOF');

/// Checkpoint witness signer keys (`COLIBRI_CHECKPOINT_WITNESS_KEYS`).
String? resolveCheckpointWitnessKeys() => readEnv('COLIBRI_CHECKPOINT_WITNESS_KEYS');

/// Whether verbose ZK debug logging is enabled (`COLIBRI_DEBUG_ZK`).
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
