# colibri_flutter example

Minimal Flutter app that uses the [colibri_flutter](..) plugin (**Colibri v2**, `0.2.x`).

Run:

```bash
flutter pub get
flutter run
```

To auto-increment the build number (4th part of the version) on every run, use:

```bash
./run.sh
```

Tap "Fetch block number" to call `eth_blockNumber` on mainnet with proof verification. Requires network access.

**Platform tips**

- Test on an **iOS simulator** and a **physical device Release** build; some FFI link issues only appear on device.
- **macOS**: needs the plugin’s bundled `libcolibri.dylib` (build desktop binaries) and `FlutterMacOS` pod dependency (plugin ≥ 0.2.1).

**Config (same as Dart `basic_usage`):** Copy `assets/.env.example` to `assets/.env`, fill in values, then rebuild. Do not commit `assets/.env`. If unset, the app uses Colibri defaults and public RPC fallbacks.
