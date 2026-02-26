import 'package:flutter/material.dart';
import 'package:colibri_flutter/colibri_flutter.dart';

void main() => runApp(const ColibriExampleApp());

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

class ExamplePage extends StatefulWidget {
  const ExamplePage({super.key});

  @override
  State<ExamplePage> createState() => _ExamplePageState();
}

class _ExamplePageState extends State<ExamplePage> {
  String _result = '';
  String _error = '';
  bool _loading = false;

  Future<void> _fetchBlockNumber() async {
    setState(() {
      _result = '';
      _error = '';
      _loading = true;
    });
    try {
      final colibri = Colibri(chainId: 1);
      final block = await colibri.rpc('eth_blockNumber', []);
      colibri.close();
      setState(() {
        _result = block?.toString() ?? 'null';
        _loading = false;
      });
    } catch (e) {
      setState(() {
        _error = '$e';
        _loading = false;
      });
    }
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
            const Text(
              'Calls eth_blockNumber (mainnet) with proof verification.',
              style: TextStyle(fontSize: 14),
            ),
            const SizedBox(height: 24),
            FilledButton(
              onPressed: _loading ? null : _fetchBlockNumber,
              child: Text(_loading ? 'Loading…' : 'Fetch block number'),
            ),
            const SizedBox(height: 16),
            if (_result.isNotEmpty)
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: SelectableText('Result: $_result'),
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
          ],
        ),
      ),
    );
  }
}
