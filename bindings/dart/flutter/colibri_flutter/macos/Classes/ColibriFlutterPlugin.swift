import FlutterMacOS

/// Minimal macOS plugin registration. Native Colibri library is loaded via Dart FFI.
public class ColibriFlutterPlugin: NSObject, FlutterPlugin {
  public static func register(with registrar: FlutterPluginRegistrar) {
    // No method channel; Colibri is used via Dart FFI and bundled libcolibri.dylib.
  }
}
