import 'dart:io';

import 'package:flutter/foundation.dart' show kIsWeb;

/// Path to the Colibri native library bundled with this Flutter plugin, if any.
///
/// On Android and iOS the library is loaded automatically (no path needed).
/// On macOS and Linux, pass this to [Colibri] so the bundled library is used:
///
/// ```dart
/// final colibri = Colibri(libraryPath: colibriFlutterLibraryPath);
/// ```
///
/// Returns `null` on web, Android, iOS, and Windows (no bundled library there yet).
String? get colibriFlutterLibraryPath {
  if (kIsWeb) return null;
  if (Platform.isMacOS) {
    final exe = Platform.resolvedExecutable;
    final dir = exe.contains('/') ? exe.substring(0, exe.lastIndexOf('/')) : '.';
    return '$dir/../Frameworks/libcolibri.dylib';
  }
  if (Platform.isLinux) {
    final exe = Platform.resolvedExecutable;
    final dir = exe.contains('/') ? exe.substring(0, exe.lastIndexOf('/')) : '.';
    return '$dir/lib/libcolibri.so';
  }
  return null;
}
