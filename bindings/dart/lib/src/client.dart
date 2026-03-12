// Conditional export: default is client_web (web); when dart.library.ffi is
// available (VM/mobile/desktop) use the FFI implementation. Default must be
// the non-FFI URI so the web compiler never loads the FFI package.
export 'client_web.dart' if (dart.library.ffi) 'package:colibri_stateless_ffi/colibri_stateless_ffi.dart' show Colibri;
