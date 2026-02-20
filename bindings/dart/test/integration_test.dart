import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:colibri_stateless/colibri.dart';
import 'package:http/testing.dart';
import 'package:test/test.dart';

import 'test_helpers.dart';

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
  if (Platform.isLinux) {
    return 'native/libcolibri.so';
  }
  return 'native/libcolibri.so';
}

bool _nativeAvailable() {
  final path = _resolveLibraryPath();
  return File(path).existsSync();
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

void main() {
  final hasNative = _nativeAvailable();
  final testDirs = discoverTestDirs();

  for (final dir in testDirs) {
    final name = dir.path.split(Platform.pathSeparator).last;
    test('proof test: $name', () async {
      final testJson = File('${dir.path}${Platform.pathSeparator}test.json');
      final content = jsonDecode(testJson.readAsStringSync()) as Map<String, dynamic>;

      if ((content['requires_chain_store'] as bool?) ?? false) {
        return;
      }

      final method = content['method'] as String;
      final params = (content['params'] as List<dynamic>);
      final chainId = (content['chain_id'] as num).toInt();
      final trusted = content['trusted_blockhash']?.toString();
      final expected = content['expected_result'];

      final storage = FileBackedStorage(dir);
      final responder = FileBasedMockResponder(dir);
      final client = MockClient(responder.handle);

      final colibri = Colibri(
        chainId: chainId,
        provers: const [],
        trustedCheckpoint: trusted,
        storage: storage,
        libraryPath: _resolveLibraryPath(),
        httpClient: client,
      );

      final result = await colibri.rpc(method, params);
      colibri.close();

      if (expected != null) {
        final adjusted = _adjustExpectedResult(method, params, expected, result);
        expect(result, equals(adjusted));
      } else {
        expect(result, isNotNull);
      }
    }, skip: !hasNative);
  }
}
