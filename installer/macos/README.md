: Installer

:: MacOS

This guide explains how to install, configure, and uninstall Colibri Server on macOS.

## Requirements

- macOS 10.15 (Catalina) or newer
- Administrator access (sudo privileges)
- At least 500 MB free disk space
- **Optional:** Memcached for caching (recommended for production)

## Installation

### Download

Download the latest installer package from the [GitHub Releases](https://github.com/corpus-core/colibri-stateless/releases) page:

```bash
curl -L -O https://github.com/corpus-core/colibri-stateless/releases/latest/download/colibri-server-1.0.0.pkg
```

### Install via Command Line

```bash
sudo installer -pkg colibri-server-1.0.0.pkg -target /
```

### Install via Finder

1. Double-click the downloaded `.pkg` file
2. Follow the installation wizard
3. Enter your administrator password when prompted

### What Gets Installed

The installer places files in the following locations:

- **Binary**: `/usr/local/bin/colibri-server`
- **Configuration**: `/usr/local/etc/colibri/server.conf`
- **Launch Daemon**: `/Library/LaunchDaemons/io.corpuscore.colibri-server.plist`
- **Log Files**: `/var/log/colibri-server.log`, `/var/log/colibri-server.error.log`
- **Data Directory**: `/var/lib/colibri/`

### Automatic Service Start

The Colibri Server is automatically installed as a LaunchDaemon and starts:
- Immediately after installation
- Automatically on system boot

The service runs in the background and listens on port **8090** by default.

## Configuration

### Edit Configuration File

```bash
sudo nano /usr/local/etc/colibri/server.conf
```

### Key Configuration Parameters

```ini
# Server port
PORT=8090

# Blockchain (1=Mainnet, 11155111=Sepolia, 17000=Holesky)
CHAIN_ID=1

# RPC endpoints (comma-separated)
RPC=https://eth-mainnet.g.alchemy.com/v2/YOUR_KEY

# Beacon Chain API endpoints (comma-separated)
BEACON=https://lodestar-mainnet.chainsafe.io/

# Web UI (0=disabled, 1=enabled) - Only enable on trusted networks!
WEB_UI_ENABLED=0
```

### Reload Configuration

After changing the configuration, reload the service:

```bash
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
```

### Enable Memcached (Optional, Recommended for Production)

Memcached significantly improves performance by caching external RPC/Beacon API requests.

**Install and start:**

```bash
# Install via Homebrew
brew install memcached

# Start memcached service
brew services start memcached

# Or start manually
memcached -d -m 64 -p 11211
```

**Configure:**

```bash
sudo nano /usr/local/etc/colibri/server.conf
# Set: MEMCACHED_HOST=localhost
```

**Reload server:**

```bash
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
```

**Verify it's working:**

```bash
# Check memcached is running
brew services list | grep memcached

# Check server logs
tail -n 20 /var/log/colibri-server.log | grep memcached
```

### Enable Web UI (Optional)

To enable the web-based configuration interface:

1. Edit the config file:
   ```bash
   sudo nano /usr/local/etc/colibri/server.conf
   ```

2. Set `WEB_UI_ENABLED=1`

3. Reload the service (see above)

4. Access the UI at: http://localhost:8090/config.html

**⚠️ Security Warning**: Only enable the Web UI on trusted local networks. It has no authentication and should never be exposed to the internet.

## Service Management

### Check Service Status

```bash
# Check if service is loaded
sudo launchctl list | grep colibri

# View recent logs
tail -n 50 /var/log/colibri-server.log
tail -n 50 /var/log/colibri-server.error.log
```

### Start/Stop Service Manually

```bash
# Stop service (it will restart automatically due to KeepAlive)
sudo launchctl stop io.corpuscore.colibri-server

# Completely unload service
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist

# Load and start service
sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
```

### View Live Logs

```bash
# Follow main log
tail -f /var/log/colibri-server.log

# Follow error log
tail -f /var/log/colibri-server.error.log
```

## Uninstallation

### Automatic Uninstallation (Recommended)

Use the uninstall script:

```bash
# Download uninstall script
curl -O https://raw.githubusercontent.com/corpus-core/colibri-stateless/main/installer/macos/uninstall.sh
chmod +x uninstall.sh
sudo ./uninstall.sh
```

The script will:
1. Stop and unload the service
2. Remove the binary and LaunchDaemon
3. Ask if you want to remove configuration files and logs

### Manual Uninstallation

If you prefer to uninstall manually:

```bash
# 1. Stop and unload the service
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist

# 2. Remove LaunchDaemon
sudo rm /Library/LaunchDaemons/io.corpuscore.colibri-server.plist

# 3. Remove binary
sudo rm /usr/local/bin/colibri-server

# 4. Remove configuration (optional)
sudo rm -rf /usr/local/etc/colibri

# 5. Remove logs (optional)
sudo rm /var/log/colibri-server.log
sudo rm /var/log/colibri-server.error.log

# 6. Remove data directory (optional)
sudo rm -rf /var/lib/colibri

# 7. Remove package receipt
sudo pkgutil --forget io.corpuscore.colibri-server
```

See [UNINSTALL.md](./UNINSTALL.md) for detailed uninstallation instructions.

## Troubleshooting

### Service Won't Start

1. **Check logs for errors:**
   ```bash
   tail -n 100 /var/log/colibri-server.error.log
   ```

2. **Common issues:**
   - Port 8090 already in use → Change `PORT` in config file
   - Invalid RPC endpoints → Check network connectivity
   - Config file syntax error → Validate config file format

3. **Test manually:**
   ```bash
   /usr/local/bin/colibri-server -c /usr/local/etc/colibri/server.conf
   ```

### Port Already in Use

Check what's using port 8090:

```bash
sudo lsof -i :8090
```

Either stop that process or change the port in `/usr/local/etc/colibri/server.conf`.

### Permission Denied Errors

Ensure log files and data directory are writable:

```bash
sudo chmod 755 /var/lib/colibri
sudo chmod 644 /var/log/colibri-server.log
sudo chmod 644 /var/log/colibri-server.error.log
```

### Check Installation Status

Use the diagnostic script to check your installation:

```bash
curl -O https://raw.githubusercontent.com/corpus-core/colibri-stateless/main/installer/macos/check_installation.sh
chmod +x check_installation.sh
./check_installation.sh
```

## Building from Source

If you want to build the installer package yourself:

```bash
cd installer/macos
./build_pkg.sh

# Or with custom version:
./build_pkg.sh 1.2.3
```

Requirements:
- Xcode Command Line Tools
- CMake (`brew install cmake`)

## Support

- **Documentation**: https://corpus-core.gitbook.io/specification-colibri-stateless
- **Issues**: https://github.com/corpus-core/colibri-stateless/issues
- **Email**: simon@corpus.io

## License

The Colibri core library is licensed under the MIT License.

The server component is dual-licensed:
- **PolyForm Noncommercial License 1.0.0** for non-commercial use
- **Commercial License** required for commercial use (contact simon@corpus.io)

