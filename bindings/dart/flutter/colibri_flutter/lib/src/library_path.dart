import 'dart:io';

/// Path to the Colibri native library bundled with this Flutter plugin, if any.
///
/// On Android and iOS the library is loaded automatically (no path needed).
/// On macOS and Linux, pass this to [Colibri] so the bundled library is used:
///
/// ```dart
/// final colibri = Colibri(libraryPath: colibriFlutterLibraryPath);
/// ```
///
/// Returns `null` on Android and iOS (loaded automatically by the plugin).
String? get colibriFlutterLibraryPath {
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
  if (Platform.isWindows) {
    final exe = Platform.resolvedExecutable;
    final dir = exe.contains(r'\') ? exe.substring(0, exe.lastIndexOf(r'\')) : '.';
    return '$dir\\colibri.dll';
  }
  return null;
}
