/// Integration test that compares Dart results against C fixtures.
import 'dart:convert';
import 'dart:io';

import 'package:test/test.dart';

void main() {
  test('Dart results match C fixtures', () async {
    final result = await Process.run(
      Platform.resolvedExecutable,
      ['run', 'tool/compare_results.dart'],
      workingDirectory: Directory.current.path,
    );

    if (result.exitCode != 0) {
      fail('compare_results.dart failed:\n${result.stderr}');
    }

    final failures = <String>[];
    for (final line in const LineSplitter().convert(result.stdout as String)) {
      Map<String, dynamic> event;
      try {
        event = jsonDecode(line) as Map<String, dynamic>;
      } catch (_) {
        continue;
      }
      if (event['skipped'] == true) {
        continue;
      }
      if (event['passed'] == false) {
        final name = event['name']?.toString() ?? 'unknown';
        failures.add(name);
      }
    }

    if (failures.isNotEmpty) {
      final buffer = StringBuffer('Mismatched results:\n');
      for (final failure in failures.take(5)) {
        buffer.writeln('- $failure');
      }
      if (failures.length > 5) {
        buffer.writeln('... and ${failures.length - 5} more');
      }
      fail(buffer.toString());
    }
  });
}
