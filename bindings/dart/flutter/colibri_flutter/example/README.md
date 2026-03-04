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

**Config (same as Dart `basic_usage`):** Edit `assets/.env` to set `COLIBRI_PROVER`, `COLIBRI_ETH_RPC`, `COLIBRI_ZK_PROOF`, `COLIBRI_DEBUG_ZK`, `COLIBRI_CHECKPOINT_WITNESS_KEYS`. Then rebuild. If unset, the app uses Colibri defaults and the same public RPC fallbacks as the Dart example.
