import 'dart:io';

import 'package:colibri_stateless/colibri.dart';
import 'package:test/test.dart';

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
}
