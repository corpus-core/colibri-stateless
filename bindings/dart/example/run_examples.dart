/// Import UTF-8 decoding for streaming process output.
import 'dart:convert';

/// Import file and process utilities for listing and running examples.
import 'dart:io';

/// Metadata describing known examples for a richer menu.
class _ExampleInfo {
  /// Create a menu entry for the given filename and description.
  const _ExampleInfo(this.fileName, this.description);

  /// The file name under `example/`.
  final String fileName;

  /// Human-friendly description shown in the menu.
  final String description;
}

/// Map of example file names to descriptions.
const _knownExamples = <String, _ExampleInfo>{
  'basic_usage.dart': _ExampleInfo(
    'basic_usage.dart',
    'Minimal proofed RPC call (eth_blockNumber).',
  ),
  'proof_verify.dart': _ExampleInfo(
    'proof_verify.dart',
    'Manual proof creation and verification.',
  ),
  'custom_storage.dart': _ExampleInfo(
    'custom_storage.dart',
    'Custom storage implementation for caching.',
  ),
  'unproofable_rpc.dart': _ExampleInfo(
    'unproofable_rpc.dart',
    'Call an unproofable RPC method.',
  ),
  'read_block.dart': _ExampleInfo(
    'read_block.dart',
    'Read a block by number and print fields.',
  ),
  'transaction_receipt.dart': _ExampleInfo(
    'transaction_receipt.dart',
    'Fetch and inspect a transaction receipt.',
  ),
  'read_logs.dart': _ExampleInfo(
    'read_logs.dart',
    'Read logs with an address + block filter.',
  ),
  'contract_call.dart': _ExampleInfo(
    'contract_call.dart',
    'Call a smart contract via eth_call.',
  ),
};

/// ANSI color helpers (disabled when ANSI is not supported).
final bool _useAnsi = stdout.supportsAnsiEscapes;
const String _ansiReset = '\x1B[0m';

String _color(String text, String code) {
  if (!_useAnsi) {
    return text;
  }
  return '\x1B[${code}m$text$_ansiReset';
}

/// Build a visual separator for readable output blocks.
String _separator(String title) {
  final line = '─' * 60;
  return _color('$line\n$title\n$line', '90');
}

/// List available examples and run selected ones in a loop.
Future<void> main(List<String> args) async {
  final examplesDir = Directory('example');
  if (!examplesDir.existsSync()) {
    stderr.writeln('Expected to run from bindings/dart directory.');
    exit(1);
  }

  final exampleFiles = examplesDir
      .listSync()
      .whereType<File>()
      .where((file) => file.path.endsWith('.dart'))
      .where((file) => !file.path.endsWith('run_examples.dart'))
      .where((file) => !file.path.endsWith('example_env.dart'))
      .toList()
    ..sort((a, b) => a.path.compareTo(b.path));

  if (exampleFiles.isEmpty) {
    stdout.writeln('No examples found in ${examplesDir.path}.');
    return;
  }

  if (args.isNotEmpty) {
    final target = args.first.trim();
    final match = exampleFiles.firstWhere(
      (file) => file.path.endsWith('/$target') || file.path.endsWith('\\$target'),
      orElse: () => File(''),
    );
    if (!match.existsSync()) {
      stderr.writeln('Example not found: $target');
      _printList(exampleFiles);
      exit(1);
    }
    await _runExample(match.path);
    return;
  }

  while (true) {
    _printList(exampleFiles);
    stdout.writeln('Enter a number to run, or "q" to quit.');
    stdout.write('Selection: ');
    final input = stdin.readLineSync()?.trim();
    if (input == null || input.isEmpty) {
      stderr.writeln('No selection provided.');
      continue;
    }
    if (input.toLowerCase() == 'q') {
      stdout.writeln('Exiting.');
      return;
    }

    final index = int.tryParse(input);
    if (index == null || index < 1 || index > exampleFiles.length) {
      stderr.writeln('Invalid selection: $input');
      continue;
    }

    await _runExample(exampleFiles[index - 1].path);
    stdout.writeln('');
  }
}

/// Print a numbered list with optional descriptions.
void _printList(List<File> examples) {
  stdout.writeln('Available examples:');
  for (var i = 0; i < examples.length; i++) {
    final name = examples[i].path.split(Platform.pathSeparator).last;
    final info = _knownExamples[name];
    if (info == null) {
      stdout.writeln('  ${i + 1}. $name');
    } else {
      stdout.writeln('  ${i + 1}. ${info.fileName} - ${info.description}');
    }
  }
  stdout.writeln('');
}

/// Run the selected example using `dart run`.
Future<void> _runExample(String path) async {
  stdout.writeln(_separator('Running example: $path'));
  final process = await Process.start('dart', ['run', path], mode: ProcessStartMode.normal);

  process.stdout.transform(utf8.decoder).listen((data) {
    stdout.write(_color(data, '36'));
  });
  process.stderr.transform(utf8.decoder).listen((data) {
    stderr.write(_color(data, '31'));
  });

  final exitCode = await process.exitCode;
  if (exitCode != 0) {
    stderr.writeln('Example failed with exit code $exitCode.');
    exit(exitCode);
  }
  stdout.writeln(_separator('Example finished: $path'));
}
