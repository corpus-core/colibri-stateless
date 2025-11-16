# Kona-P2P OP Stack Bridge

Eine **echte OP-Stack-kompatible** Preconfirmation Bridge basierend auf `kona-p2p`.

## 🎯 Warum diese Bridge?

Die ursprüngliche Go-Bridge hatte ein **Protokoll-Kompatibilitätsproblem**:

| **Problem** | **Go-Bridge (libp2p)** | **Kona-Bridge (kona-p2p)** |
|-------------|-------------------------|----------------------------|
| Discovery   | Kademlia DHT           | ✅ discv5 (wie echte Sequencer) |
| Bootnodes   | Multiaddr/enode        | ✅ ENR (Ethereum Node Records) |
| Netzwerk    | libp2p GossipSub       | ✅ Kona P2P (Helios-kompatibel) |
| Ergebnis    | ❌ Findet keine echten Preconfs | ✅ **Echte OP-Stack-Kompatibilität** |

## 🚀 Schnellstart

### 1. **Build**
```bash
cd src/chains/op/kona_bridge
./build.sh
```

### 2. **Standalone verwenden**
```bash
# Base Chain
./build/default/bin/kona_bridge --chain-id 8453 --output-dir ./preconfs

# OP Mainnet
./build/default/bin/kona_bridge --chain-id 10 --output-dir ./preconfs

# Mit custom Sequencer
./build/default/bin/kona_bridge \
  --chain-id 8453 \
  --sequencer-address "0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a" \
  --output-dir ./preconfs
```

### 3. **In C-Server integrieren**
```c
#include "kona_bridge.h"

// Konfiguration erstellen
KonaBridgeConfig config = KONA_BRIDGE_CONFIG_BASE();
config.output_dir = "./preconfs";

// Bridge starten
KonaBridgeHandle* bridge = kona_bridge_start(&config);
if (bridge == NULL) {
    // Fallback zu HTTP-Modus
}

// Später: Bridge stoppen
kona_bridge_stop(bridge);
```

## 🏗️ Architektur

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   C-Server      │    │   Kona-Bridge    │    │  OP-Stack       │
│                 │    │                  │    │  Sequencers     │
│ ┌─────────────┐ │    │ ┌──────────────┐ │    │                 │
│ │ HTTP Server │ │    │ │ kona-p2p     │ │◄──►│ discv5 + ENR   │
│ │ Verifier    │ │◄──►│ │ Network      │ │    │ GossipSub      │
│ │ Storage     │ │    │ │ Discovery    │ │    │ Preconfs       │
│ └─────────────┘ │    │ └──────────────┘ │    │                 │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

## 📋 Features

- ✅ **Echte OP-Stack-Kompatibilität** mit `discv5`/ENR
- ✅ **Multi-Chain-Support** (OP, Base, Unichain, etc.)
- ✅ **C-Integration** für bestehende Server
- ✅ **Standalone-Binary** für Tests
- ✅ **Automatic Cleanup** mit konfigurierbarer TTL
- ✅ **Signature Verification** mit Chain-spezifischen Sequencern
- ✅ **ZSTD Compression** (kompatibel mit Go-Bridge)
- ✅ **Atomic File Operations** für Konsistenz

## 🔧 Konfiguration

### Chain-spezifische Defaults

| Chain | ID | Sequencer Address | ENR Bootnodes |
|-------|----|--------------------|---------------|
| **OP Mainnet** | 10 | `0xAAAA45d9549EDA09E70937013520214382Ffc4A2` | Standard |
| **Base** | 8453 | `0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a` | Standard |
| **Unichain** | 130 | `0x833C6f278474A78658af91aE8edC926FE33a230e` | Custom ENRs |

### Environment Variables
```bash
export RUST_LOG=info                    # Logging Level
export KONA_BRIDGE_OUTPUT_DIR=./preconfs  # Output Directory
export KONA_BRIDGE_TTL_MINUTES=30       # TTL für Preconfs
```

## 🔍 Monitoring

### Statistiken abrufen
```c
KonaBridgeStats stats;
if (kona_bridge_get_stats(bridge, &stats) == 0) {
    printf("Connected peers: %u\n", stats.connected_peers);
    printf("Received preconfs: %u\n", stats.received_preconfs);
    printf("Processed preconfs: %u\n", stats.processed_preconfs);
    printf("Failed preconfs: %u\n", stats.failed_preconfs);
}
```

### Log-Output
```
🚀 Starting Kona-P2P OP Stack Bridge
⛓️  Chain ID: 8453
🔐 Expected sequencer: 0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a
🔍 Discovery: 0.0.0.0:9090
📡 Gossip: 0.0.0.0:9091
🌐 Starting kona-p2p network...
✅ Network started successfully!
🎧 Listening for OP Stack preconfirmations...
🎉 RECEIVED PRECONF! Block #12345678 Hash: 0x1234...
✅ Preconf processed successfully
```

## 🛠️ Development

### Build Requirements
- **Rust** 1.70+ (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`)
- **CMake** 3.16+
- **C Compiler** (gcc/clang)

### Build Commands
```bash
# Full build (Rust + C integration)
./build.sh

# Nur Rust-Library
cargo build --release

# Nur Standalone-Binary
cargo build --release --bin kona_bridge

# Tests
cargo test
```

### Integration in CMake
```cmake
# In Ihrem CMakeLists.txt
add_subdirectory(src/chains/op/kona_bridge)
target_link_libraries(your_target kona_bridge_integration)
```

## 🚨 Migration von Go-Bridge

### 1. **Backup bestehender Preconfs**
```bash
cp -r ./preconfs ./preconfs_backup
```

### 2. **Server-Code anpassen**
```c
// Alt: Go-Bridge
// system("./bin/opg_bridge -chain-id 8453 -use-http false");

// Neu: Kona-Bridge
#include "kona_bridge.h"
KonaBridgeConfig config = KONA_BRIDGE_CONFIG_BASE();
KonaBridgeHandle* bridge = kona_bridge_start(&config);
```

### 3. **Kompatibilität**
- ✅ **File-Format**: Identisch (ZSTD + Signature)
- ✅ **Metadata**: Kompatibles JSON-Format
- ✅ **Naming**: Gleiche `block_{chain}_{number}.raw` Konvention
- ✅ **Symlinks**: `latest.raw` wird weiterhin erstellt

## 🐛 Troubleshooting

### Bridge startet nicht
```bash
# Prüfe Rust-Installation
rustc --version

# Prüfe Ports
netstat -tulpn | grep -E "(9090|9091)"

# Prüfe Logs
RUST_LOG=debug ./bin/kona_bridge --chain-id 8453
```

### Keine Preconfs empfangen
```bash
# Prüfe Netzwerk-Verbindung
./bin/kona_bridge --chain-id 8453 --disc-port 9090 --gossip-port 9091

# Vergleiche mit HTTP-Modus (als Referenz)
./bin/opg_bridge -chain-id 8453 -use-http true -http-endpoint <helios-url>
```

### Performance-Tuning
```toml
# Cargo.toml - Release-Optimierungen
[profile.release]
opt-level = 3
lto = true
codegen-units = 1
```

## 📚 Weitere Ressourcen

- **Kona Documentation**: https://op-rs.github.io/kona/
- **OP Stack P2P Specs**: https://specs.optimism.io/protocol/rollup-node-p2p.html
- **discv5 Protocol**: https://github.com/ethereum/devp2p/blob/master/discv5/discv5.md

---

**🎉 Mit der Kona-Bridge haben Sie echte OP-Stack-Kompatibilität!**  
Keine Protokoll-Inkompatibilitäten mehr - direkte Verbindung zu den echten Sequencern.
