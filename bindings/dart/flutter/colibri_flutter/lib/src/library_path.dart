import 'dart:io';

String? _macosBundledLibraryPath(String frameworksDir) {
  const candidates = [
    'libcolibri.dylib',
    'libcolibri.framework/libcolibri',
  ];
  for (final name in candidates) {
    final path = '$frameworksDir/$name';
    if (File(path).existsSync()) {
      return path;
    }
  }
  try {
    for (final entity in Directory(frameworksDir).listSync()) {
      if (entity is! File) {
        continue;
      }
      final name = entity.uri.pathSegments.last;
      if (name.startsWith('libcolibri') && name.endsWith('.dylib')) {
        return entity.path;
      }
    }
  } catch (_) {
    // Fall through to default path for error messages.
  }
  return '$frameworksDir/libcolibri.dylib';
}

/// Path to the Colibri native library bundled with this Flutter plugin, if any.
///
/// On Android and iOS the library is loaded automatically (no path needed).
/// On macOS and Linux, pass this to [Colibri] so the bundled library is used:
///
/// ```dart
/// final colibri = Colibri(libraryPath: colibriFlutterLibraryPath);
/// ```
///
/// Returns `null` on Android, iOS, and Windows (no bundled library there yet).
String? get colibriFlutterLibraryPath {
  if (Platform.isMacOS) {
    final exe = Platform.resolvedExecutable;
    final dir = exe.contains('/') ? exe.substring(0, exe.lastIndexOf('/')) : '.';
    final frameworksDir = '$dir/../Frameworks';
    return _macosBundledLibraryPath(frameworksDir);
  }
  if (Platform.isLinux) {
    final exe = Platform.resolvedExecutable;
    final dir = exe.contains('/') ? exe.substring(0, exe.lastIndexOf('/')) : '.';
    return '$dir/lib/libcolibri.so';
  }
  return null;
}
