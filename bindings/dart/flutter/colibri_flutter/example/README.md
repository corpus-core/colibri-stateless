# colibri_flutter example

Minimal Flutter app that uses the [colibri_flutter](..) plugin.

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

**Config (same as Dart `basic_usage`):** Copy `assets/.env.example` to `assets/.env`, fill in values, then rebuild. Do not commit `assets/.env`. If unset, the app uses Colibri defaults and public RPC fallbacks.
