#include <flutter_linux/flutter_linux.h>

// Minimal plugin registration. Colibri is used via Dart FFI and bundled libcolibri.so.
void colibri_flutter_register_with_registrar(FlPluginRegistrar* registrar) {
  (void)registrar;
}
