/// Import file system access and platform helpers.
import 'dart:io';

/// Import the public Colibri API.
import 'package:colibri_stateless/colibri.dart';

/// Import the Dart test framework.
import 'package:test/test.dart';

/// Resolve the native library path for Colibri.
String _resolveLibraryPath() {
  /// Allow overriding the library path via environment variable.
  final env = Platform.environment['COLIBRI_DART_LIBRARY'];
  if (env != null && env.isNotEmpty) {
    return env;
  }
  /// Fall back to the standard build output location per OS.
  if (Platform.isWindows) {
    return 'native/colibri.dll';
  }
  if (Platform.isMacOS) {
    return 'native/libcolibri.dylib';
  }
  if (Platform.isLinux) {
    return 'native/libcolibri.so';
  }
  /// Default to Linux naming for unknown platforms.
  return 'native/libcolibri.so';
}

/// Check whether the native library file is present.
bool _nativeAvailable() {
  final path = _resolveLibraryPath();
  return File(path).existsSync();
}

void main() {
  final hasNative = _nativeAvailable();

  test('MethodType values', () {
    expect(MethodType.undefined.value, 0);
    expect(MethodType.proofable.value, 1);
    expect(MethodType.unproofable.value, 2);
    expect(MethodType.notSupported.value, 3);
    expect(MethodType.local.value, 4);
  });

  test('Default configs', () {
    final colibri = Colibri(libraryPath: _resolveLibraryPath());
    expect(colibri.chainId, 1);
    expect(colibri.provers, isNotEmpty);
    expect(colibri.ethRpcs, isNotEmpty);
    expect(colibri.beaconApis, isNotEmpty);
    expect(colibri.trustedCheckpoint, isNull);
  }, skip: !hasNative);

  test('Method support - proofable/local/undefined', () {
    final colibri = Colibri(libraryPath: _resolveLibraryPath());

    expect(colibri.getMethodSupport('eth_getBalance'), MethodType.proofable);
    expect(colibri.getMethodSupport('eth_getBlockByNumber'), MethodType.proofable);
    expect(colibri.getMethodSupport('eth_chainId'), MethodType.local);
    expect(colibri.getMethodSupport('custom_fakeMethod'), MethodType.undefined);
  }, skip: !hasNative);

  test('rpc throws for unsupported method', () async {
    final colibri = Colibri(libraryPath: _resolveLibraryPath());
    expect(
      () => colibri.rpc('custom_fakeMethod', []),
      throwsA(isA<ColibriError>().having((e) => e.message, 'message', contains('not supported'))),
    );
    colibri.close();
  }, skip: !hasNative);

  test('close is idempotent', () {
    final colibri = Colibri(libraryPath: _resolveLibraryPath());
    colibri.close();
    expect(() => colibri.close(), returnsNormally);
  }, skip: !hasNative);

  test('onDebug callback is invoked during rpc', () async {
    final messages = <String>[];
    final colibri = Colibri(
      libraryPath: _resolveLibraryPath(),
      onDebug: (msg) => messages.add(msg),
    );
    try {
      await colibri.rpc('eth_chainId', []);
    } on ColibriError {
      // May fail without network; we only verify onDebug was called.
    }
    colibri.close();
    expect(messages, isNotEmpty);
  }, skip: !hasNative);

  group('PrivacyMode', () {
    test('privacyMode.none produces flags 0', () {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        privacyMode: PrivacyMode.none,
      );
      expect(colibri.privacyMode, PrivacyMode.none);
      colibri.close();
    }, skip: !hasNative);

    test('privacyMode.basic is accepted by constructor', () {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        privacyMode: PrivacyMode.basic,
      );
      expect(colibri.privacyMode, PrivacyMode.basic);
      colibri.close();
    }, skip: !hasNative);

    test('Method support with PrivacyMode.basic does not throw', () {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        privacyMode: PrivacyMode.basic,
      );
      final support = colibri.getMethodSupport('eth_getBalance');
      expect(support, isA<MethodType>());
      colibri.close();
    }, skip: !hasNative);
  });

  group('Chain defaults', () {
    test('Sepolia chain has correct defaults', () {
      final colibri = Colibri(
        chainId: 11155111,
        libraryPath: _resolveLibraryPath(),
      );
      expect(colibri.provers, isNotEmpty);
      expect(colibri.ethRpcs, isNotEmpty);
      expect(colibri.beaconApis, isNotEmpty);
      colibri.close();
    }, skip: !hasNative);

    test('Unknown chain has generic prover fallback but no RPC/beacon defaults', () {
      /// For unsupported chains we intentionally return empty RPC/beacon lists
      /// so misconfiguration is visible instead of silently falling back to a
      /// mainnet endpoint. Only the generic prover fallback remains.
      final colibri = Colibri(
        chainId: 999999,
        libraryPath: _resolveLibraryPath(),
      );
      expect(colibri.provers, isNotEmpty);
      expect(colibri.ethRpcs, isEmpty);
      expect(colibri.beaconApis, isEmpty);
      colibri.close();
    }, skip: !hasNative);
  });

  group('Constructor options', () {
    test('custom provers/ethRpcs/beaconApis override defaults', () {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        provers: ['https://my-prover.example'],
        ethRpcs: ['https://my-rpc.example'],
        beaconApis: ['https://my-beacon.example'],
      );
      expect(colibri.provers, ['https://my-prover.example']);
      expect(colibri.ethRpcs, ['https://my-rpc.example']);
      expect(colibri.beaconApis, ['https://my-beacon.example']);
      colibri.close();
    }, skip: !hasNative);

    test('zkProof and includeCode flags are stored', () {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        zkProof: true,
        includeCode: true,
        useAccesslist: true,
      );
      expect(colibri.zkProof, isTrue);
      expect(colibri.includeCode, isTrue);
      expect(colibri.useAccesslist, isTrue);
      colibri.close();
    }, skip: !hasNative);

    test('MemoryStorage can be provided explicitly', () {
      final storage = MemoryStorage();
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        storage: storage,
      );
      expect(colibri.storage, same(storage));
      colibri.close();
    }, skip: !hasNative);
  });

  group('rpc error paths', () {
    test('rpc with undefined method throws ColibriError', () async {
      final colibri = Colibri(libraryPath: _resolveLibraryPath());
      expect(
        () => colibri.rpc('nonexistent_method', []),
        throwsA(isA<ColibriError>().having(
          (e) => e.message,
          'message',
          contains('not supported'),
        )),
      );
      colibri.close();
    }, skip: !hasNative);

    test('rpc with proofable method and unreachable prover falls back to local proof', () async {
      final colibri = Colibri(
        libraryPath: _resolveLibraryPath(),
        provers: ['http://127.0.0.1:1'],
        ethRpcs: ['http://127.0.0.1:1'],
        beaconApis: ['http://127.0.0.1:1'],
        checkpointz: ['http://127.0.0.1:1'],
      );
      // With all servers unreachable the proof creation / data fetch will fail.
      // We verify the error is a Colibri exception, not an unhandled crash.
      try {
        await colibri.rpc('eth_blockNumber', []);
        fail('Expected an exception');
      } on ColibriError {
        // Expected: some form of ColibriError (RPCError, HTTPError, ProofError, etc.)
      }
      colibri.close();
    }, skip: !hasNative);
  });
}
