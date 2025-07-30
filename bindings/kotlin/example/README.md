# Colibri Android Example App

This is a minimal Android example app that demonstrates how to use the Colibri library for Ethereum RPC calls.

## Features

- Simple UI with block number display
- Refresh button to fetch the latest Ethereum block number
- Uses the `eth_blockNumber` RPC method via Colibri
- Error handling and loading states

## What it demonstrates

- How to initialize the Colibri library in an Android app
- How to make RPC calls using the `rpc()` method
- How to handle asynchronous operations with Kotlin coroutines
- Basic error handling for network and Colibri-specific exceptions

## Usage

1. The app automatically fetches the current Ethereum block number on startup
2. Use the "Refresh" button to get the latest block number
3. The status text shows when the last update occurred

## Dependencies

- Colibri AAR (built from the parent project)
- Standard Android dependencies (AppCompat, ConstraintLayout, etc.)
- Kotlin Coroutines for async operations

## Building

This app is designed to be built as part of the CI pipeline, but you can also build it locally:

```bash
cd bindings/kotlin
./gradlew -b example/build.gradle build
```

Note: You need to build the Colibri AAR first, as the example app depends on it.

## Configuration

The app is configured to use public Ethereum RPC endpoints:
- ETH RPC: `rpc.ankr.com/eth`, `eth.public-rpc.com`, `ethereum.publicnode.com`
- Beacon API: `lodestar-mainnet.chainsafe.io`, `beaconstate.info`, `mainnet.beacon.publicnode.com`
- Proofer: `c4.incubed.net`

These endpoints are hardcoded for simplicity in this example app.