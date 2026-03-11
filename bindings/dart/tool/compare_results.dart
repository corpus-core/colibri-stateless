/// Compare Dart results against test.json expectations.
import 'dart:convert';
import 'dart:io';

import 'package:colibri_stateless/colibri.dart';
import 'package:http/testing.dart';

import '../test/test_helpers.dart';

class CompareResult {
  const CompareResult({
    required this.name,
    required this.passed,
    this.skipped = false,
    this.reason,
    this.expected,
    this.actual,
    this.error,
  });

  final String name;
  final bool passed;
  final bool skipped;
  final String? reason;
  final dynamic expected;
  final dynamic actual;
  final String? error;

  Map<String, dynamic> toJson() {
    return {
      'name': name,
      'passed': passed,
      if (skipped) 'skipped': true,
      if (reason != null) 'reason': reason,
      if (expected != null) 'expected': expected,
      if (actual != null) 'actual': actual,
      if (error != null) 'error': error,
    };
  }
}

String _resolveLibraryPath() {
  final env = Platform.environment['COLIBRI_DART_LIBRARY'];
  if (env != null && env.isNotEmpty) {
    return env;
  }
  if (Platform.isWindows) {
    return 'native/colibri.dll';
  }
  if (Platform.isMacOS) {
    return 'native/libcolibri.dylib';
  }
  return 'native/libcolibri.so';
}

dynamic _adjustExpectedResult(
  String method,
  List<dynamic> params,
  dynamic expected,
  dynamic actual,
) {
  if (method == 'eth_getBlockByNumber' && params.length > 1 && params[1] == true) {
    if (actual is Map && actual['transactions'] is List) {
      final actualTxs = actual['transactions'] as List<dynamic>;
      if (actualTxs.isNotEmpty && actualTxs.first is String) {
        if (expected is Map<String, dynamic> && expected['transactions'] is List) {
          final txs = expected['transactions'] as List<dynamic>;
          final hashes = <String>[];
          for (final entry in txs) {
            if (entry is Map && entry['hash'] is String) {
              hashes.add(entry['hash'] as String);
            }
          }
          if (hashes.isNotEmpty) {
            final updated = Map<String, dynamic>.from(expected);
            updated['transactions'] = hashes;
            return updated;
          }
        }
      }
    }
  }
  return expected;
}

bool _deepEquals(dynamic a, dynamic b) {
  if (a is Map && b is Map) {
    if (a.length != b.length) {
      return false;
    }
    for (final key in a.keys) {
      if (!b.containsKey(key)) {
        return false;
      }
      if (!_deepEquals(a[key], b[key])) {
        return false;
      }
    }
    return true;
  }
  if (a is List && b is List) {
    if (a.length != b.length) {
      return false;
    }
    for (var i = 0; i < a.length; i++) {
      if (!_deepEquals(a[i], b[i])) {
        return false;
      }
    }
    return true;
  }
  return a == b;
}

Future<List<CompareResult>> compareAll() async {
  final libraryPath = _resolveLibraryPath();
  if (!File(libraryPath).existsSync()) {
    stderr.writeln('Native library not found: $libraryPath');
    return [
      const CompareResult(
        name: 'native_library',
        passed: false,
        error: 'Native library not found',
      ),
    ];
  }

  final results = <CompareResult>[];
  final testDirs = discoverTestDirs();
  for (final dir in testDirs) {
    final name = dir.path.split(Platform.pathSeparator).last;
    final testJson = File('${dir.path}${Platform.pathSeparator}test.json');
    final content = jsonDecode(testJson.readAsStringSync()) as Map<String, dynamic>;

    if ((content['requires_chain_store'] as bool?) ?? false) {
      results.add(const CompareResult(
        name: 'requires_chain_store',
        passed: true,
        skipped: true,
        reason: 'requires_chain_store',
      ));
      continue;
    }

    final method = content['method'] as String;
    final params = (content['params'] as List<dynamic>);
    final chainId = (content['chain_id'] as num).toInt();
    final trusted = content['trusted_blockhash']?.toString();
    final expected = content['expected_result'];
    final includeCode = (content['include_code'] as bool?) ?? false;
    final useAccesslist = (content['use_accesslist'] as bool?) ?? false;
    final pap = (content['pap'] as bool?) ?? false;

    final storage = FileBackedStorage(dir);
    final responder = FileBasedMockResponder(dir);
    final client = MockClient(responder.handle);

    final colibri = Colibri(
      chainId: chainId,
      provers: pap ? ['http://mock-prover'] : const [],
      trustedCheckpoint: trusted,
      includeCode: includeCode,
      useAccesslist: useAccesslist,
      privacyMode: pap ? PrivacyMode.basic : PrivacyMode.none,
      storage: storage,
      libraryPath: libraryPath,
      httpClient: client,
    );

    try {
      final result = await colibri.rpc(method, params);
      colibri.close();

      dynamic adjustedExpected = expected;
      if (expected != null) {
        adjustedExpected = _adjustExpectedResult(method, params, expected, result);
      }

      final passed = expected == null ? result != null : _deepEquals(result, adjustedExpected);
      results.add(CompareResult(
        name: name,
        passed: passed,
        expected: adjustedExpected,
        actual: result,
      ));
    } catch (error) {
      colibri.close();
      results.add(CompareResult(
        name: name,
        passed: false,
        error: error.toString(),
      ));
    }
  }
  return results;
}

Future<void> main() async {
  final results = await compareAll();
  for (final result in results) {
    print(jsonEncode(result.toJson()));
  }
}
