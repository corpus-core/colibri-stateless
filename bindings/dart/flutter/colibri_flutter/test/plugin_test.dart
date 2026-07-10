import 'package:colibri_flutter/colibri_flutter.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('plugin exports Colibri and colibriFlutterLibraryPath', () {
    expect(Colibri, isNotNull);
    // colibriFlutterLibraryPath is null on Android/iOS/Windows, or a path on macOS/Linux
    final path = colibriFlutterLibraryPath;
    expect(path == null || path.isNotEmpty, isTrue);
  });
}
