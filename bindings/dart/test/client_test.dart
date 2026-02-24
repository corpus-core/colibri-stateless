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

/// Smoke tests for enum values and client defaults.
void main() {
  /// Determine once if the native library exists so we can skip native tests.
  final hasNative = _nativeAvailable();

  /// Verify that enum values match the C bindings.
  test('MethodType values', () {
    expect(MethodType.undefined.value, 0);
    expect(MethodType.proofable.value, 1);
    expect(MethodType.unproofable.value, 2);
    expect(MethodType.notSupported.value, 3);
    expect(MethodType.local.value, 4);
  });

  /// Verify the default client configuration for mainnet.
  test('Default configs', () {
    /// Instantiate a client using the default chain (1) and library path.
    final colibri = Colibri(libraryPath: _resolveLibraryPath());
    /// Default chain id should be Ethereum mainnet.
    expect(colibri.chainId, 1);
    /// Default endpoints should be set for prover, execution, and beacon APIs.
    expect(colibri.provers, isNotEmpty);
    expect(colibri.ethRpcs, isNotEmpty);
    expect(colibri.beaconApis, isNotEmpty);
    /// No trusted checkpoint unless explicitly provided.
    expect(colibri.trustedCheckpoint, isNull);
  }, skip: !hasNative);

  /// Verify method support classification via the native library.
  test('Method support - proofable/local/undefined', () {
    /// Create a client with the native library available.
    final colibri = Colibri(libraryPath: _resolveLibraryPath());

    /// Proofable methods should be classified as proofable.
    expect(colibri.getMethodSupport('eth_getBalance'), MethodType.proofable);
    expect(colibri.getMethodSupport('eth_getBlockByNumber'), MethodType.proofable);
    /// Local methods are handled without proof.
    expect(colibri.getMethodSupport('eth_chainId'), MethodType.local);
    /// Unknown methods are undefined.
    expect(colibri.getMethodSupport('custom_fakeMethod'), MethodType.undefined);
  }, skip: !hasNative);

  /// RPC with unsupported/undefined method throws [ColibriError].
  test('rpc throws for unsupported method', () async {
    final colibri = Colibri(libraryPath: _resolveLibraryPath());
    expect(
      () => colibri.rpc('custom_fakeMethod', []),
      throwsA(isA<ColibriError>().having((e) => e.message, 'message', contains('not supported'))),
    );
    colibri.close();
  }, skip: !hasNative);
}
