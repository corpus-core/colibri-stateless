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

### Web (Chrome)

RPC/Prover/Beacon endpoints block cross-origin requests from `localhost`, so the browser shows "Failed to fetch" unless CORS is relaxed for local dev. Run Chrome with web security disabled:

```bash
flutter run -d chrome --web-browser-flag "--disable-web-security"
```

Or in VS Code/Cursor: choose the launch configuration **"Chrome (web, CORS disabled)"** and run (F5). Use this only for local testing.

**Config (same as Dart `basic_usage`):** Edit `assets/.env` to set `COLIBRI_PROVER`, `COLIBRI_ETH_RPC`, `COLIBRI_ZK_PROOF`, `COLIBRI_DEBUG_ZK`, `COLIBRI_CHECKPOINT_WITNESS_KEYS`. Then rebuild. If unset, the app uses Colibri defaults and the same public RPC fallbacks as the Dart example.
