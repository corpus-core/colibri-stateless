# Installing Colibri Server with Homebrew

This guide explains how to install, configure, and manage the Colibri Server using Homebrew on macOS.

## Prerequisites

- macOS 10.15 (Catalina) or later
- [Homebrew](https://brew.sh/) installed

## Installation

### Step 1: Add the Colibri Tap

```bash
brew tap corpus-core/colibri
```

### Step 2: Install Colibri Server

```bash
brew install colibri-server
```

This will install:
- `colibri-server` - The main server executable
- `colibri-prover` - CLI tool for generating proofs
- `colibri-verifier` - CLI tool for verifying proofs
- `colibri-ssz` - CLI tool for SSZ encoding/decoding

### Step 3: Configure the Server

The configuration file is located at:
```
/opt/homebrew/etc/colibri/server.conf
```

Edit the configuration file to customize your setup:
```bash
nano /opt/homebrew/etc/colibri/server.conf
```

#### Important Configuration Options:

**Network Binding (Security):**
```ini
# For local use only (secure, recommended for Metamask):
HOST=127.0.0.1

# For remote access (use with caution):
HOST=0.0.0.0
```

**Port:**
```ini
PORT=8090
```

**Chain Configuration:**
```ini
CHAIN_ID=1  # 1=Ethereum Mainnet, 11155111=Sepolia, 17000=Holesky
```

**RPC Endpoints:**
```ini
RPC=https://eth.llamarpc.com,https://rpc.payload.de
BEACON=https://lodestar-mainnet.chainsafe.io/
```

## Service Management

Colibri Server can run as a background service (daemon) using Homebrew's built-in service management.

### Start the Service

```bash
brew services start colibri-server
```

The server will:
- Start automatically on system boot
- Restart automatically if it crashes
- Run in the background

### Stop the Service

```bash
brew services stop colibri-server
```

### Restart the Service

```bash
brew services restart colibri-server
```

### Check Service Status

```bash
brew services list | grep colibri
```

or

```bash
brew services info colibri-server
```

## Viewing Logs

Logs are stored in:
```
/opt/homebrew/var/log/colibri-server.log       # Standard output
/opt/homebrew/var/log/colibri-server.error.log # Error output
```

### Tail the logs in real-time:

```bash
tail -f /opt/homebrew/var/log/colibri-server.log
```

or

```bash
tail -f /opt/homebrew/var/log/colibri-server.error.log
```

### View last 100 lines:

```bash
tail -100 /opt/homebrew/var/log/colibri-server.log
```

## Running Manually (Without Service)

If you prefer to run the server manually (e.g., for testing):

```bash
colibri-server -f /opt/homebrew/etc/colibri/server.conf
```

Press `Ctrl+C` to stop the server.

## CLI Tools Usage

### Generating Proofs

```bash
colibri-prover eth_getBalance '["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb"]' > proof.ssz
```

### Verifying Proofs

```bash
colibri-verifier eth_getBalance proof.ssz
```

### SSZ Operations

```bash
colibri-ssz -t BeaconBlock -h block.ssz
```

For detailed usage, run:
```bash
colibri-prover --help
colibri-verifier --help
colibri-ssz --help
```

## Updating Colibri Server

```bash
brew update
brew upgrade colibri-server
```

After upgrading, restart the service:
```bash
brew services restart colibri-server
```

## Uninstallation

### Stop and Remove the Service

```bash
brew services stop colibri-server
```

### Uninstall the Package

```bash
brew uninstall colibri-server
```

### Remove Configuration (Optional)

```bash
rm -rf /opt/homebrew/etc/colibri
rm -f /opt/homebrew/var/log/colibri-server*.log
```

### Remove the Tap (Optional)

```bash
brew untap corpus-core/colibri
```

## Troubleshooting

### Server Won't Start

1. **Check the logs:**
   ```bash
   tail -f /opt/homebrew/var/log/colibri-server.error.log
   ```

2. **Verify configuration:**
   ```bash
   colibri-server --help
   ```

3. **Check if port is already in use:**
   ```bash
   lsof -i :8090
   ```

### Permission Issues

Homebrew services run as your user, so no `sudo` is required. If you encounter permission issues:

```bash
chmod 644 /opt/homebrew/etc/colibri/server.conf
```

### Service Not Starting on Boot

```bash
brew services restart colibri-server
```

This will re-register the service with the system.

## Using Colibri as Metamask RPC Provider

1. Start the service: `brew services start colibri-server`
2. In Metamask, go to Settings → Networks → Add Network
3. Configure:
   - **Network Name:** Colibri Local
   - **RPC URL:** `http://127.0.0.1:8090`
   - **Chain ID:** 1 (or your configured chain)
   - **Currency Symbol:** ETH

4. Metamask will now use your local Colibri server for all RPC requests, providing verified and trustless data.

## Advanced Configuration

### Performance Tuning with Memcached

For better performance, install and enable Memcached:

```bash
brew install memcached
brew services start memcached
```

Then edit your config:
```ini
MEMCACHED_HOST=localhost
MEMCACHED_PORT=11211
```

Restart the service:
```bash
brew services restart colibri-server
```

### Web Configuration UI

⚠️ **Security Warning:** Only enable on trusted networks!

Edit your config:
```ini
WEB_UI_ENABLED=1
```

Access at: `http://127.0.0.1:8090/config.html`

## Support

- **Documentation:** https://corpus-core.gitbook.io/specification-colibri-stateless
- **GitHub:** https://github.com/corpus-core/colibri-stateless
- **Issues:** https://github.com/corpus-core/colibri-stateless/issues
- **Email:** jork@corpus.io

## License

- **Core Library:** MIT License
- **Server Component:** PolyForm Noncommercial License 1.0.0 (Commercial licenses available)

For commercial licensing, contact: jork@corpus.io

