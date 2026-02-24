/// Import the public Colibri API (types and errors).
import 'package:colibri_stateless/colibri.dart';

/// Import the Dart test framework.
import 'package:test/test.dart';

/// Unit tests for [MethodType], error types, and [DataRequest.fromJson].
void main() {
  group('MethodType', () {
    test('fromValue returns matching enum for known values', () {
      expect(MethodType.fromValue(0), MethodType.undefined);
      expect(MethodType.fromValue(1), MethodType.proofable);
      expect(MethodType.fromValue(2), MethodType.unproofable);
      expect(MethodType.fromValue(3), MethodType.notSupported);
      expect(MethodType.fromValue(4), MethodType.local);
    });

    test('fromValue returns undefined for unknown value', () {
      expect(MethodType.fromValue(99), MethodType.undefined);
      expect(MethodType.fromValue(-1), MethodType.undefined);
    });
  });

  group('ColibriError', () {
    test('toString returns message when details is null', () {
      final err = ColibriError('Something failed');
      expect(err.toString(), 'Something failed');
    });

    test('toString returns message when details is empty', () {
      final err = ColibriError('Fail', details: '');
      expect(err.toString(), 'Fail');
    });

    test('toString returns message and details when details is set', () {
      final err = ColibriError('RPC failed', details: 'connection refused');
      expect(err.toString(), 'RPC failed: connection refused');
    });
  });

  group('ProofError', () {
    test('toString includes details when provided', () {
      final err = ProofError('Prover error', details: 'timeout');
      expect(err.toString(), 'Prover error: timeout');
    });
  });

  group('VerificationError', () {
    test('toString includes details when provided', () {
      final err = VerificationError('Invalid proof', details: 'bad signature');
      expect(err.toString(), 'Invalid proof: bad signature');
    });
  });

  group('RPCError', () {
    test('stores code and uses in message when details set', () {
      final err = RPCError('Execution reverted', code: -32000, details: 'revert');
      expect(err.code, -32000);
      expect(err.toString(), 'Execution reverted: revert');
    });
  });

  group('HTTPError', () {
    test('stores statusCode and uses in message when details set', () {
      final err = HTTPError('Server error', statusCode: 503, details: 'overloaded');
      expect(err.statusCode, 503);
      expect(err.toString(), 'Server error: overloaded');
    });
  });

  group('DataRequest.fromJson', () {
    test('parses payload as string into map', () {
      final data = <String, dynamic>{
        'req_ptr': 42,
        'url': '/rpc',
        'method': 'POST',
        'encoding': 'json',
        'type': 'eth_rpc',
        'exclude_mask': 0,
        'chain_id': 1,
        'payload': '{"method":"eth_blockNumber","params":[]}',
      };
      final req = DataRequest.fromJson(data);
      expect(req.payload, isA<Map<String, dynamic>>());
      expect(req.payload!['method'], 'eth_blockNumber');
      expect(req.payload!['params'], []);
    });

    test('parses exclude_mask as string', () {
      final data = <String, dynamic>{
        'req_ptr': 1,
        'url': '',
        'method': 'get',
        'encoding': 'json',
        'type': 'eth_rpc',
        'exclude_mask': '2',
        'chain_id': 1,
      };
      final req = DataRequest.fromJson(data);
      expect(req.excludeMask, 2);
    });

    test('exclude_mask string invalid defaults to 0', () {
      final data = <String, dynamic>{
        'req_ptr': 1,
        'url': '',
        'method': 'get',
        'encoding': 'json',
        'type': 'eth_rpc',
        'exclude_mask': 'invalid',
        'chain_id': 1,
      };
      final req = DataRequest.fromJson(data);
      expect(req.excludeMask, 0);
    });

    test('parses chain_id as string', () {
      final data = <String, dynamic>{
        'req_ptr': 1,
        'url': '',
        'method': 'get',
        'encoding': 'json',
        'type': 'eth_rpc',
        'exclude_mask': 0,
        'chain_id': '11155111',
      };
      final req = DataRequest.fromJson(data);
      expect(req.chainId, 11155111);
    });
  });
}
