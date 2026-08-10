import 'dart:io';

import 'package:colibri_flutter/colibri_flutter.dart';
import 'package:flutter_test/flutter_test.dart';

/// Resolve the native library path for tests.
/// Prefer COLIBRI_DART_LIBRARY (CI / local override), then plugin path, then platform default.
String? _resolveLibraryPath() {
  final env = Platform.environment['COLIBRI_DART_LIBRARY'];
  if (env != null && env.isNotEmpty && File(env).existsSync()) {
    return env;
  }
  final pluginPath = colibriFlutterLibraryPath;
  if (pluginPath != null && pluginPath.isNotEmpty && File(pluginPath).existsSync()) {
    return pluginPath;
  }
  if (Platform.isWindows) return 'native/colibri.dll';
  if (Platform.isMacOS) return 'native/libcolibri.dylib';
  return 'native/libcolibri.so';
}

bool _nativeAvailable() {
  final path = _resolveLibraryPath();
  return path != null && File(path).existsSync();
}

void main() {
  final hasNative = _nativeAvailable();

  test('MethodType values', () {
    expect(MethodType.undefined.value, 0);
    expect(MethodType.proofable.value, 1);
    expect(MethodType.local.value, 4);
  });

  test('Client default config when native available', () {
    final path = _resolveLibraryPath();
    if (path == null) return;
    final colibri = Colibri(libraryPath: path);
    expect(colibri.chainId, 1);
    expect(colibri.provers, isNotEmpty);
    expect(colibri.ethRpcs, isNotEmpty);
    expect(colibri.beaconApis, isNotEmpty);
    colibri.close();
  }, skip: !hasNative);

  test('getMethodSupport via plugin client', () {
    final path = _resolveLibraryPath();
    if (path == null) return;
    final colibri = Colibri(libraryPath: path);
    expect(colibri.getMethodSupport('eth_getBalance'), MethodType.proofable);
    expect(colibri.getMethodSupport('eth_chainId'), MethodType.local);
    expect(colibri.getMethodSupport('custom_fakeMethod'), MethodType.undefined);
    colibri.close();
  }, skip: !hasNative);

  test('rpc throws for unsupported method', () async {
    final path = _resolveLibraryPath();
    if (path == null) return;
    final colibri = Colibri(libraryPath: path);
    expect(
      () => colibri.rpc('custom_fakeMethod', []),
      throwsA(isA<ColibriError>().having((e) => e.message, 'message', contains('not supported'))),
    );
    colibri.close();
  }, skip: !hasNative);

  test('close is idempotent', () {
    final path = _resolveLibraryPath();
    if (path == null) return;
    final colibri = Colibri(libraryPath: path);
    colibri.close();
    expect(() => colibri.close(), returnsNormally);
  }, skip: !hasNative);
}
