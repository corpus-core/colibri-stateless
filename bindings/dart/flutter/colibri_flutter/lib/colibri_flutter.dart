/// Flutter wrapper that re-exports the Colibri Dart API.
///
/// On desktop (macOS, Linux), use [colibriFlutterLibraryPath] so the bundled
/// native library is used:
///
/// ```dart
/// final colibri = Colibri(libraryPath: colibriFlutterLibraryPath);
/// ```
library colibri_flutter;

export 'package:colibri_stateless/colibri.dart';
export 'src/library_path.dart';
