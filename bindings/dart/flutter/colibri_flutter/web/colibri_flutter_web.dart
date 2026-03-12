// Flutter web implementation for colibri_flutter.
//
// On web, Colibri uses the JS/WASM bridge (see client_web in colibri_stateless).
// This plugin does not provide native code for web; it only ensures the package
// is compatible with Flutter web. The app must load the Colibri WASM bundle and
// colibri_web_bridge.js (e.g. from this package's assets) before using Colibri.
// See README for web setup.

import 'package:flutter_web_plugins/flutter_web_plugins.dart';

void registerWith(Registrar registrar) {
  // No native registration needed for web. Colibri is used via colibri_stateless,
  // which uses the JS bridge when running on web. The host app should include
  // the Colibri WASM script and colibri_web_bridge.js in index.html (or load
  // them from assets/packages/colibri_flutter/).
}
