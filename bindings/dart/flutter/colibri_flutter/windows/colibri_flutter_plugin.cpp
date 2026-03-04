#include <flutter/plugin_registrar_windows.h>

// Minimal plugin registration. Colibri is used via Dart FFI and bundled colibri.dll.
void ColibriFlutterPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  (void)registrar;
}
