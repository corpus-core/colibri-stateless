import 'package:flutter/material.dart';
import 'package:colibri_flutter/colibri_flutter.dart';

import 'env_loader.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await loadExampleEnv();
  runApp(const ColibriExampleApp());
}

/// Root widget with Material 3 theme.
class ColibriExampleApp extends StatelessWidget {
  const ColibriExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Colibri Flutter Example',
      theme: ThemeData(useMaterial3: true),
      home: const ExamplePage(),
    );
  }
}

/// Available RPC test methods for the combo-button.
enum RpcTest {
  blockNumber('Block Number', 'eth_blockNumber'),
  block('Block', 'eth_getBlockByNumber'),
  logs('Logs', 'eth_getLogs');

  const RpcTest(this.label, this.method);
  final String label;
  final String method;
}

/// Main page with combo-button for running RPC tests and a scrollable log.
class ExamplePage extends StatefulWidget {
  const ExamplePage({super.key});

  @override
  State<ExamplePage> createState() => _ExamplePageState();
}

class _ExamplePageState extends State<ExamplePage> {
  RpcTest _selectedTest = RpcTest.blockNumber;
  String _result = '';
  String _error = '';
  bool _loading = false;
  final List<String> _log = [];

  void _addLog(String message) {
    final line = '${DateTime.now().toString().substring(11, 23)} $message';
    setState(() => _log.insert(0, line));
  }

  Colibri _createClient() {
    final provers = resolveProvers();
    final ethRpcs = resolveEthRpcs(fallback: defaultPublicEthRpcs());
    final zkFromEnv = resolveZkProof();
    final witnessKeys = resolveCheckpointWitnessKeys();
    final wantZk = zkFromEnv || (witnessKeys != null && witnessKeys.isNotEmpty && witnessKeys != '0x');
    final colibri = Colibri(
      chainId: 1,
      libraryPath: colibriFlutterLibraryPath,
      provers: provers,
      ethRpcs: ethRpcs,
      zkProof: wantZk,
      checkpointWitnessKeys: witnessKeys,
      logProverRequests: resolveZkDebug(),
      storage: MemoryStorage(),
      onDebug: _addLog,
    );
    _addLog('zkProof: $wantZk | provers: ${colibri.provers.length}');
    return colibri;
  }

  Future<void> _runTest() async {
    setState(() {
      _result = '';
      _error = '';
      _log.clear();
      _loading = true;
    });
    _addLog('Start ${_selectedTest.method}');
    try {
      final colibri = _createClient();
      try {
        switch (_selectedTest) {
          case RpcTest.blockNumber:
            await _testBlockNumber(colibri);
          case RpcTest.block:
            await _testBlock(colibri);
          case RpcTest.logs:
            await _testLogs(colibri);
        }
      } finally {
        colibri.close();
      }
    } catch (e) {
      _handleError(e);
    }
    setState(() => _loading = false);
  }

  Future<void> _testBlockNumber(Colibri colibri) async {
    final result = await colibri.rpc('eth_blockNumber', []);
    if (result != null) {
      _setResult(formatBlockNumber(result));
    } else {
      _setError('No result');
    }
  }

  Future<void> _testBlock(Colibri colibri) async {
    final result = await colibri.rpc('eth_getBlockByNumber', ['latest', false]);
    if (result is Map) {
      final number = formatBlockNumber(result['number']);
      final hash = result['hash'] ?? '';
      final txCount = (result['transactions'] as List?)?.length ?? 0;
      final timestamp = result['timestamp'];
      String time = '';
      if (timestamp is String && timestamp.startsWith('0x')) {
        final epoch = int.tryParse(timestamp.substring(2), radix: 16);
        if (epoch != null) {
          time = DateTime.fromMillisecondsSinceEpoch(epoch * 1000).toIso8601String();
        }
      }
      _setResult('Block #$number\n'
          'Hash: $hash\n'
          'Transactions: $txCount\n'
          'Time: $time');
    } else {
      _setError('Unexpected result: $result');
    }
  }

  Future<void> _testLogs(Colibri colibri) async {
    _addLog('Fetching latest block number first…');
    final blockHex = await colibri.rpc('eth_blockNumber', []);
    if (blockHex == null) {
      _setError('Could not get block number');
      return;
    }
    final blockNum = int.tryParse(blockHex.toString().replaceFirst('0x', ''), radix: 16) ?? 0;
    final fromBlock = '0x${(blockNum - 5).toRadixString(16)}';
    final toBlock = blockHex.toString();
    _addLog('Querying logs from $fromBlock to $toBlock (last 5 blocks)');
    final result = await colibri.rpc('eth_getLogs', [
      {'fromBlock': fromBlock, 'toBlock': toBlock}
    ]);
    if (result is List) {
      final count = result.length;
      final buf = StringBuffer('$count log entries (blocks $fromBlock..$toBlock)\n');
      for (var i = 0; i < result.length && i < 10; i++) {
        final log = result[i] as Map;
        final addr = (log['address'] ?? '').toString();
        final topics = (log['topics'] as List?)?.length ?? 0;
        buf.writeln('#$i  addr=${_shorten(addr)}  topics=$topics');
      }
      if (count > 10) buf.writeln('… and ${count - 10} more');
      _setResult(buf.toString().trimRight());
    } else {
      _setError('Unexpected result: $result');
    }
  }

  void _setResult(String text) {
    _addLog('Done.');
    setState(() => _result = text);
  }

  void _setError(String text) {
    setState(() => _error = text);
  }

  void _handleError(Object e) {
    final msg = e.toString();
    final isNetwork = msg.contains('Timeout') ||
        msg.contains('SocketException') ||
        msg.contains('Connection') ||
        msg.contains('Failed host lookup');
    _addLog('Error: $e');
    _setError(isNetwork ? 'Netzwerk nicht erreichbar: $msg' : msg);
  }

  static String _shorten(String hex) {
    if (hex.length <= 14) return hex;
    return '${hex.substring(0, 8)}…${hex.substring(hex.length - 4)}';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Colibri Flutter Example')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            MenuAnchor(
              menuChildren: RpcTest.values.map((t) {
                return MenuItemButton(
                  onPressed: _loading
                      ? null
                      : () => setState(() => _selectedTest = t),
                  leadingIcon: t == _selectedTest ? const Icon(Icons.check, size: 18) : const SizedBox(width: 18),
                  child: Text('${t.label}  (${t.method})'),
                );
              }).toList(),
              builder: (context, controller, child) {
                final theme = Theme.of(context);
                return Material(
                  color: _loading ? theme.colorScheme.primary.withAlpha(200) : theme.colorScheme.primary,
                  borderRadius: BorderRadius.circular(24),
                  clipBehavior: Clip.antiAlias,
                  child: InkWell(
                    onTap: _loading ? null : _runTest,
                    child: SizedBox(
                      height: 48,
                      child: Row(
                        children: [
                          Expanded(
                            child: Padding(
                              padding: const EdgeInsets.only(left: 20),
                              child: Row(
                                mainAxisAlignment: MainAxisAlignment.center,
                                children: [
                                  if (_loading)
                                    const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                                  else
                                    const Icon(Icons.play_arrow, color: Colors.white, size: 20),
                                  const SizedBox(width: 8),
                                  Text(
                                    _selectedTest.label,
                                    style: theme.textTheme.labelLarge?.copyWith(color: Colors.white),
                                  ),
                                ],
                              ),
                            ),
                          ),
                          VerticalDivider(width: 1, thickness: 1, color: Colors.white.withAlpha(60), indent: 10, endIndent: 10),
                          InkWell(
                            onTap: _loading ? null : () {
                              controller.isOpen ? controller.close() : controller.open();
                            },
                            child: const Padding(
                              padding: EdgeInsets.symmetric(horizontal: 12),
                              child: Icon(Icons.arrow_drop_down, color: Colors.white),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 16),
            if (_result.isNotEmpty)
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: SelectableText(
                    _result,
                    style: const TextStyle(fontFamily: 'monospace'),
                  ),
                ),
              ),
            if (_error.isNotEmpty)
              Card(
                color: Theme.of(context).colorScheme.errorContainer,
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: SelectableText('Error: $_error'),
                ),
              ),
            const SizedBox(height: 12),
            if (_log.isNotEmpty)
              Expanded(
                child: Card(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Padding(
                        padding: const EdgeInsets.all(12),
                        child: Text('Log (newest first)', style: Theme.of(context).textTheme.titleSmall),
                      ),
                      const Divider(height: 1),
                      Expanded(
                        child: SingleChildScrollView(
                          padding: const EdgeInsets.all(12),
                          child: SelectableText(
                            _log.join('\n'),
                            style: Theme.of(context).textTheme.bodySmall?.copyWith(fontFamily: 'monospace'),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
