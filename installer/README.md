: Installer

This directory contains installer packages and build scripts for deploying Colibri Server as a system service on Linux, macOS, and Windows.

## Overview

The installer system provides:
- **Configuration file support**: `server.conf` for easy configuration management
- **System service integration**: systemd (Linux), LaunchDaemon (macOS), Windows Service
- **Automatic startup**: Server starts automatically on system boot
- **Security**: Services run with restricted permissions
- **Firewall rules**: Automatic configuration for the server port

## Directory Structure

```
installer/
├── config/
│   ├── server.conf.template    # Template with placeholders
│   └── server.conf.default     # Default configuration
├── scripts/
│   ├── systemd/                # Linux systemd service
│   ├── launchd/                # macOS LaunchDaemon
│   └── windows/                # Windows service scripts
├── linux/
│   ├── debian/                 # Debian/Ubuntu package files
│   ├── rpm/                    # RPM package specs
│   ├── build_deb.sh           # Build Debian package
│   └── build_rpm.sh           # Build RPM package
├── macos/
│   ├── scripts/               # Pre/post-install scripts
│   └── build_pkg.sh          # Build macOS .pkg
└── windows/
    ├── colibri.wxs           # WiX installer definition
    ├── build_installer.ps1   # Build MSI installer
    └── install_service.ps1   # Manual service installation
```

## Configuration File

The server can be configured via `server.conf` file in addition to environment variables and command-line arguments.

**Priority order** (lowest to highest):
1. Default values (hardcoded)
2. Config file (`/etc/colibri/server.conf` on Linux)
3. Environment variables
4. Command-line arguments

**Config file locations:**
- **Linux**: `/etc/colibri/server.conf`
- **macOS**: `/usr/local/etc/colibri/server.conf`
- **Windows**: `%PROGRAMDATA%\Colibri\server.conf` (usually `C:\ProgramData\Colibri\server.conf`)

### Configuration Parameters

See `config/server.conf.default` for all available options:
- `PORT` - HTTP server port (default: 8090)
- `CHAIN_ID` - Blockchain ID (1=Mainnet, 11155111=Sepolia, 17000=Holesky)
- `RPC` - Comma-separated RPC endpoints
- `BEACON` - Comma-separated Beacon API endpoints
- `MEMCACHED_HOST` / `MEMCACHED_PORT` - Memcached configuration
- And many more...

## CMake Build Flag

### The INSTALLER Flag

Installer packages are built using a dedicated CMake flag that is **disabled by default** to avoid unnecessary builds during regular development.

#### Usage

```bash
# Build with installer support
cmake -DHTTP_SERVER=ON -DINSTALLER=ON ..
make

# Regular build (no installer)
cmake -DHTTP_SERVER=ON ..
make
```

#### Requirements

- `INSTALLER=ON` requires `HTTP_SERVER=ON`
- If `INSTALLER=ON` but `HTTP_SERVER=OFF`, a warning is shown and installer targets are skipped

#### What Gets Enabled

When `INSTALLER=ON` is set:
- CPack configuration is loaded
- Installer-specific targets are created
- Platform-specific package metadata is configured
- Installation rules are set up

#### Default Behavior

By default (`INSTALLER=OFF`):
- Regular server builds work normally
- No installer-specific configuration is loaded
- Build times are faster for development
- No CPack dependencies required

#### Why This Design?

- **Faster developer builds**: Developers don't need installer dependencies
- **Explicit intent**: Installer builds are only created when explicitly requested
- **CI optimization**: Avoids unnecessary work in non-installer workflows
- **Cleaner separation**: Clear distinction between development and distribution builds

#### Example Workflows

**Developer building for testing:**
```bash
mkdir build && cd build
cmake -DHTTP_SERVER=ON -DPROVER=ON ..
make -j4 colibri-server
./default/bin/colibri-server
```

**Packaging for distribution:**
```bash
mkdir build && cd build
cmake -DHTTP_SERVER=ON -DPROVER=ON -DINSTALLER=ON ..
make -j4 colibri-server
cpack  # Creates platform-specific package
```

**CI/CD automated build:**
```bash
cd installer/macos
./build_pkg.sh 1.2.3  # Automatically sets INSTALLER=ON
```

**Note:** All build scripts in `installer/linux/`, `installer/macos/`, and `installer/windows/` automatically set `-DINSTALLER=ON`, so you don't need to set it manually when using those scripts.

## Building Installers

### Linux (Debian/Ubuntu)

**Requirements:**
- `dpkg-dev`, `debhelper`, `cmake`, `gcc`, `g++`

```bash
cd installer/linux
./build_deb.sh
```

**Output:** `../colibri-server_1.0.0-1_amd64.deb`

**Install:**
```bash
sudo dpkg -i colibri-server_1.0.0-1_amd64.deb
sudo apt-get install -f  # Install dependencies if needed
```

### Linux (RPM - RedHat/CentOS/Fedora)

**Requirements:**
- `rpm-build`, `rpmdevtools`, `cmake`, `gcc`, `gcc-c++`

```bash
cd installer/linux
./build_rpm.sh
```

**Output:** `~/rpmbuild/RPMS/x86_64/colibri-server-1.0.0-1.x86_64.rpm`

**Install:**
```bash
sudo rpm -ivh ~/rpmbuild/RPMS/x86_64/colibri-server-1.0.0-1.x86_64.rpm
# Or on Fedora/RHEL 8+:
sudo dnf install ~/rpmbuild/RPMS/x86_64/colibri-server-1.0.0-1.x86_64.rpm
```

### macOS

**Requirements:**
- Xcode Command Line Tools
- CMake

```bash
cd installer/macos
./build_pkg.sh
```

**Output:** `../../colibri-server-1.0.0.pkg`

**Install:**
```bash
sudo installer -pkg colibri-server-1.0.0.pkg -target /
# Or double-click the .pkg file in Finder
```

### Windows

**Requirements:**
- Visual Studio 2022 (or newer)
- WiX Toolset v3.11+ (https://wixtoolset.org/)
- CMake

```powershell
cd installer\windows
.\build_installer.ps1
```

**Output:** `..\..\build\colibri-server-1.0.0.msi`

**Install:**
```powershell
# As Administrator:
msiexec /i colibri-server-1.0.0.msi
# Or double-click the MSI file
```

## Service Management

### Linux (systemd)

```bash
# Start service
sudo systemctl start colibri-server

# Stop service
sudo systemctl stop colibri-server

# Restart service
sudo systemctl restart colibri-server

# View status
sudo systemctl status colibri-server

# View logs
sudo journalctl -u colibri-server -f

# Enable/disable auto-start
sudo systemctl enable colibri-server
sudo systemctl disable colibri-server
```

### macOS (LaunchDaemon)

```bash
# Start service
sudo launchctl start io.corpuscore.colibri-server

# Stop service
sudo launchctl stop io.corpuscore.colibri-server

# View logs
tail -f /var/log/colibri-server.log
tail -f /var/log/colibri-server.error.log

# Load/unload service
sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
```

### Windows (Service)

```powershell
# Start service
Start-Service -Name ColibriServer

# Stop service
Stop-Service -Name ColibriServer

# Restart service
Restart-Service -Name ColibriServer

# View status
Get-Service -Name ColibriServer

# View logs (Event Viewer)
Get-EventLog -LogName Application -Source ColibriServer -Newest 50
```

## Uninstallation

### Linux (Debian/Ubuntu)
```bash
sudo apt-get remove colibri-server
# Or purge to remove config files:
sudo apt-get purge colibri-server
```

### Linux (RPM)
```bash
sudo rpm -e colibri-server
# Or on Fedora/RHEL 8+:
sudo dnf remove colibri-server
```

### macOS

**Recommended: Use the uninstall script**

```bash
cd installer/macos
sudo ./uninstall.sh
```

The script will:
- Stop and unload the service
- Remove the binary and LaunchDaemon
- Ask if you want to keep or remove config files and logs

See [macos/UNINSTALL.md](macos/UNINSTALL.md) for manual uninstallation instructions.

### Windows
```powershell
# Via Control Panel > Programs and Features
# Or via command line:
msiexec /x {Product-GUID}
```

## Customizing the Configuration

After installation, edit the configuration file:

**Linux:**
```bash
sudo nano /etc/colibri/server.conf
sudo systemctl restart colibri-server
```

**macOS:**
```bash
sudo nano /usr/local/etc/colibri/server.conf
sudo launchctl stop io.corpuscore.colibri-server
sudo launchctl start io.corpuscore.colibri-server
```

**Windows:**
```powershell
notepad C:\ProgramData\Colibri\server.conf
Restart-Service -Name ColibriServer
```

## Troubleshooting

### Service won't start

1. **Check logs:**
   - Linux: `journalctl -u colibri-server -n 50`
   - macOS: `tail -n 50 /var/log/colibri-server.error.log`
   - Windows: Event Viewer → Application logs

2. **Verify configuration:**
   - Check for syntax errors in `server.conf`
   - Ensure RPC/Beacon endpoints are accessible
   - Verify port is not already in use

3. **Check permissions:**
   - Linux: Ensure `/var/lib/colibri` is owned by `colibri` user
   - macOS: Ensure log files are writable
   - Windows: Ensure service has necessary permissions

### Port already in use

Edit the config file and change `PORT` to a different value:
```bash
# Linux/macOS
sudo sed -i 's/PORT=8090/PORT=8091/' /etc/colibri/server.conf

# Windows
# Edit C:\ProgramData\Colibri\server.conf manually
```

Then restart the service.

### Network connectivity issues

Ensure firewall rules allow incoming connections:

**Linux:**
```bash
sudo ufw allow 8090/tcp
```

**macOS:**
```bash
# Add rule via System Preferences > Security & Privacy > Firewall
```

**Windows:**
```powershell
New-NetFirewallRule -DisplayName "Colibri Server" -Direction Inbound -Protocol TCP -LocalPort 8090 -Action Allow
```

## CI/CD - Automated Installer Builds

Installer packages are automatically built via GitHub Actions on every push to `main` or `dev` branches, as well as on release tags.

### Workflow Triggers

1. **On push to main/dev:** Builds all installers and uploads them as artifacts (dev version)
2. **On version tags (v*.*.*):** Builds release versions and creates a GitHub Release with all installer packages
3. **Manual trigger:** Can be triggered manually via GitHub UI with custom version

### Version Handling

- **Release tags** (e.g., `v1.2.3`): Version extracted from tag → `colibri-server-1.2.3.pkg`
- **Branch pushes**: Uses commit SHA → `colibri-server-dev-abc1234.pkg`
- **Manual builds**: Uses provided version number

### Downloading Pre-built Installers

**From GitHub Releases:**
```bash
# Get latest release
curl -s https://api.github.com/repos/corpus-core/colibri-stateless/releases/latest \
  | grep "browser_download_url.*\.pkg" \
  | cut -d '"' -f 4 \
  | xargs curl -L -O
```

**From GitHub Actions Artifacts:**
1. Go to Actions tab → "Build Installers" workflow
2. Select the workflow run
3. Download artifacts (requires GitHub login)

### Building Locally

To build installers locally with a custom version:

```bash
# macOS
cd installer/macos
./build_pkg.sh 1.2.3

# The version parameter is optional; defaults to 1.0.0
```

## Development & Testing

To test the server without installing:

```bash
# Build server
mkdir -p build/test
cd build/test
cmake -DCMAKE_BUILD_TYPE=Release -DHTTP_SERVER=ON -DPROVER=ON -DVERIFIER=ON ../..
make -j4 colibri-server

# Create config
cp ../../installer/config/server.conf.default ./server.conf

# Run manually
./default/bin/colibri-server -c server.conf
```

## License

The Colibri core library is licensed under the MIT License.

The server component (`src/server/`) is dual-licensed:
- **PolyForm Noncommercial License 1.0.0** for non-commercial use
- **Commercial License** required for commercial use (contact jork@corpus.io)

## Support

For issues, questions, or feature requests:
- GitHub: https://github.com/corpus-core/colibri-stateless/issues
- Email: jork@corpus.io
- Documentation: https://corpus-core.gitbook.io/specification-colibri-stateless

