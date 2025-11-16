: Installer

:: Linux

This guide explains how to install, configure, and uninstall Colibri Server on Linux distributions (Debian, Ubuntu, RedHat, CentOS, Fedora).

## Requirements

- Linux distribution with systemd
- Root/sudo access
- At least 500 MB free disk space
- glibc 2.31 or newer (Ubuntu 20.04+, Debian 11+, RHEL 8+)
- **Optional:** Memcached for caching (recommended for production)

## Installation

### Debian/Ubuntu (DEB Package)

#### Download

Download the latest `.deb` package from the [GitHub Releases](https://github.com/corpus-core/colibri-stateless/releases) page:

```bash
curl -L -O https://github.com/corpus-core/colibri-stateless/releases/latest/download/colibri-server_1.0.0-1_amd64.deb
```

#### Install

```bash
# Install the package
sudo dpkg -i colibri-server_1.0.0-1_amd64.deb

# Install any missing dependencies
sudo apt-get install -f

# Verify installation
sudo systemctl status colibri-server
```

### RedHat/CentOS/Fedora (RPM Package)

#### Download

```bash
curl -L -O https://github.com/corpus-core/colibri-stateless/releases/latest/download/colibri-server-1.0.0-1.x86_64.rpm
```

#### Install

```bash
# RHEL/CentOS 7
sudo rpm -ivh colibri-server-1.0.0-1.x86_64.rpm

# RHEL/CentOS 8+, Fedora
sudo dnf install colibri-server-1.0.0-1.x86_64.rpm

# Verify installation
sudo systemctl status colibri-server
```

### What Gets Installed

The installer places files in the following locations:

- **Binary**: `/usr/bin/colibri-server`
- **Configuration**: `/etc/colibri/server.conf`
- **Systemd Service**: `/etc/systemd/system/colibri-server.service`
- **Log Files**: Accessible via `journalctl -u colibri-server`
- **Data Directory**: `/var/lib/colibri/`
- **System User**: `colibri` (created automatically)

### Automatic Service Start

The Colibri Server is automatically installed as a systemd service and:
- Starts immediately after installation
- Starts automatically on system boot
- Runs as dedicated `colibri` user (non-root)
- Restarts automatically on failure

The service listens on port **8090** by default.

## Configuration

### Edit Configuration File

```bash
sudo nano /etc/colibri/server.conf
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

# Logging level (0=Error, 1=Warning, 2=Info, 3=Debug)
LOG_LEVEL=0

# Web UI (0=disabled, 1=enabled) - Only enable on trusted networks!
WEB_UI_ENABLED=0
```

### Reload Configuration

After changing the configuration, restart the service:

```bash
sudo systemctl restart colibri-server
```

### Enable Memcached (Optional, Recommended for Production)

Memcached significantly improves performance by caching external RPC/Beacon API requests.

**Install and enable:**

```bash
# Debian/Ubuntu
sudo apt-get install memcached
sudo systemctl enable memcached
sudo systemctl start memcached

# RedHat/CentOS/Fedora
sudo dnf install memcached
sudo systemctl enable memcached
sudo systemctl start memcached
```

**Configure:**

```bash
sudo nano /etc/colibri/server.conf
# Set: MEMCACHED_HOST=localhost
sudo systemctl restart colibri-server
```

**Verify it's working:**

```bash
# Check memcached is running
sudo systemctl status memcached

# Check server is using it
sudo journalctl -u colibri-server -n 20 | grep memcached
```

### Enable Web UI (Optional)

To enable the web-based configuration interface:

1. Edit the config file:
   ```bash
   sudo nano /etc/colibri/server.conf
   ```

2. Set `WEB_UI_ENABLED=1`

3. Restart the service:
   ```bash
   sudo systemctl restart colibri-server
   ```

4. Access the UI at: http://localhost:8090/ui

**⚠️ Security Warning**: Only enable the Web UI on trusted local networks. It has no authentication and should never be exposed to the internet.

## Service Management

### Basic Commands

```bash
# Start service
sudo systemctl start colibri-server

# Stop service
sudo systemctl stop colibri-server

# Restart service
sudo systemctl restart colibri-server

# View status
sudo systemctl status colibri-server

# Enable auto-start on boot (default)
sudo systemctl enable colibri-server

# Disable auto-start on boot
sudo systemctl disable colibri-server
```

### View Logs

```bash
# View all logs
sudo journalctl -u colibri-server

# Follow live logs
sudo journalctl -u colibri-server -f

# View last 100 lines
sudo journalctl -u colibri-server -n 100

# View logs since today
sudo journalctl -u colibri-server --since today

# View logs with timestamps
sudo journalctl -u colibri-server -o short-precise
```

## Firewall Configuration

### UFW (Ubuntu/Debian)

```bash
# Allow incoming connections on port 8090
sudo ufw allow 8090/tcp

# Check status
sudo ufw status
```

### firewalld (RedHat/CentOS/Fedora)

```bash
# Allow incoming connections on port 8090
sudo firewall-cmd --permanent --add-port=8090/tcp
sudo firewall-cmd --reload

# Check status
sudo firewall-cmd --list-ports
```

### iptables

```bash
sudo iptables -A INPUT -p tcp --dport 8090 -j ACCEPT
sudo iptables-save > /etc/iptables/rules.v4
```

## Uninstallation

### Debian/Ubuntu

```bash
# Remove package (keep configuration)
sudo apt-get remove colibri-server

# Remove package and configuration files
sudo apt-get purge colibri-server

# Remove dependencies
sudo apt-get autoremove
```

### RedHat/CentOS/Fedora

```bash
# RHEL/CentOS 7
sudo rpm -e colibri-server

# RHEL/CentOS 8+, Fedora
sudo dnf remove colibri-server
```

### Manual Cleanup (if needed)

If you want to remove all traces:

```bash
# Stop service
sudo systemctl stop colibri-server
sudo systemctl disable colibri-server

# Remove service file
sudo rm /etc/systemd/system/colibri-server.service
sudo systemctl daemon-reload

# Remove files
sudo rm /usr/bin/colibri-server
sudo rm -rf /etc/colibri
sudo rm -rf /var/lib/colibri

# Remove user
sudo userdel colibri
```

## Troubleshooting

### Service Won't Start

1. **Check logs:**
   ```bash
   sudo journalctl -u colibri-server -n 50
   ```

2. **Common issues:**
   - Port 8090 already in use → Change `PORT` in config file
   - Invalid RPC endpoints → Check network connectivity
   - Config file syntax error → Validate config file format
   - Permission issues → Check `/var/lib/colibri` ownership

3. **Test manually:**
   ```bash
   sudo -u colibri /usr/bin/colibri-server -c /etc/colibri/server.conf
   ```

### Port Already in Use

Check what's using port 8090:

```bash
sudo ss -tulpn | grep 8090
# or
sudo lsof -i :8090
```

Either stop that process or change the port in `/etc/colibri/server.conf`.

### Permission Denied Errors

Ensure the colibri user owns the data directory:

```bash
sudo chown -R colibri:colibri /var/lib/colibri
sudo chmod 755 /var/lib/colibri
```

### SELinux Issues (RedHat/CentOS)

If SELinux is enabled and blocking the service:

```bash
# Check SELinux denials
sudo ausearch -m avc -ts recent

# Allow HTTP port (if needed)
sudo semanage port -a -t http_port_t -p tcp 8090

# Or temporarily disable SELinux for testing
sudo setenforce 0
```

### Service Status

Check if the service is enabled and running:

```bash
systemctl is-enabled colibri-server
systemctl is-active colibri-server
```

## Building from Source

If you want to build the package yourself:

### Debian/Ubuntu

```bash
cd installer/linux
./build_deb.sh

# Output: ../colibri-server_1.0.0-1_amd64.deb
```

Requirements:
- `dpkg-dev`, `debhelper`, `cmake`, `gcc`, `g++`

### RedHat/CentOS/Fedora

```bash
cd installer/linux
./build_rpm.sh

# Output: ~/rpmbuild/RPMS/x86_64/colibri-server-1.0.0-1.x86_64.rpm
```

Requirements:
- `rpm-build`, `rpmdevtools`, `cmake`, `gcc`, `gcc-c++`

## Support

- **Documentation**: https://corpus-core.gitbook.io/specification-colibri-stateless
- **Issues**: https://github.com/corpus-core/colibri-stateless/issues
- **Email**: simon@corpus.io

## License

The Colibri core library is licensed under the MIT License.

The server component is dual-licensed:
- **PolyForm Noncommercial License 1.0.0** for non-commercial use
- **Commercial License** required for commercial use (contact simon@corpus.io)

