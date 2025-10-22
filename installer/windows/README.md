: Installer

:: Windows

This guide explains how to install, configure, and uninstall Colibri Server on Windows.

## Requirements

- Windows 10 (version 1809) or newer, or Windows Server 2019 or newer
- Administrator privileges
- At least 500 MB free disk space
- .NET Framework 4.7.2 or newer (usually pre-installed)
- **Optional:** Memcached for caching (recommended for production)

## Installation

### Download

Download the latest MSI installer from the [GitHub Releases](https://github.com/corpus-core/colibri-stateless/releases) page:

```powershell
# Using PowerShell
Invoke-WebRequest -Uri "https://github.com/corpus-core/colibri-stateless/releases/latest/download/colibri-server-1.0.0.msi" -OutFile "colibri-server-1.0.0.msi"
```

### Install via GUI

1. Double-click the downloaded `.msi` file
2. Follow the installation wizard
3. Choose installation directory (default: `C:\Program Files\Colibri\`)
4. Configure server port and blockchain (optional, can be changed later)
5. Click "Install"
6. Allow firewall rule creation when prompted

### Install via Command Line

Open PowerShell or Command Prompt **as Administrator**:

```powershell
# Silent installation
msiexec /i colibri-server-1.0.0.msi /qn

# Installation with GUI
msiexec /i colibri-server-1.0.0.msi

# Installation with logging
msiexec /i colibri-server-1.0.0.msi /l*v install.log
```

### What Gets Installed

The installer places files in the following locations:

- **Binary**: `C:\Program Files\Colibri\colibri-server.exe`
- **Configuration**: `C:\ProgramData\Colibri\server.conf`
- **Windows Service**: Installed as `ColibriServer`
- **Logs**: Windows Event Log (Application) and `C:\ProgramData\Colibri\logs\`
- **Firewall Rule**: Allows incoming TCP connections on configured port (default: 8090)

### Automatic Service Start

The Colibri Server is automatically installed as a Windows Service and:
- Starts immediately after installation
- Starts automatically with Windows
- Runs with restricted LocalService privileges
- Restarts automatically on failure

The service listens on port **8090** by default.

## Configuration

### Edit Configuration File

Use Notepad or any text editor **as Administrator**:

```powershell
notepad C:\ProgramData\Colibri\server.conf
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

```powershell
Restart-Service -Name ColibriServer
```

### Enable Memcached (Optional, Recommended for Production)

Memcached significantly improves performance by caching external RPC/Beacon API requests.

**Install:**

1. Download memcached for Windows: https://memcached.org/downloads
2. Or use pre-built binaries: https://github.com/nono303/memcached/releases

**Run as service:**

```powershell
# Install as Windows service
sc.exe create Memcached binPath= "C:\memcached\memcached.exe -d runservice" start= auto
sc.exe start Memcached

# Or install using Chocolatey
choco install memcached
```

**Configure:**

```powershell
notepad C:\ProgramData\Colibri\server.conf
# Set: MEMCACHED_HOST=localhost
```

**Restart Colibri Server:**

```powershell
Restart-Service -Name ColibriServer
```

**Verify it's working:**

```powershell
# Check memcached is running
Get-Service -Name Memcached

# Check Colibri Server logs
Get-EventLog -LogName Application -Source ColibriServer -Newest 20 | Select-Object Message
```

### Enable Web UI (Optional)

To enable the web-based configuration interface:

1. Edit the config file:
   ```powershell
   notepad C:\ProgramData\Colibri\server.conf
   ```

2. Set `WEB_UI_ENABLED=1`

3. Restart the service:
   ```powershell
   Restart-Service -Name ColibriServer
   ```

4. Access the UI at: http://localhost:8090/ui

**⚠️ Security Warning**: Only enable the Web UI on trusted local networks. It has no authentication and should never be exposed to the internet.

## Service Management

### Using PowerShell

```powershell
# Start service
Start-Service -Name ColibriServer

# Stop service
Stop-Service -Name ColibriServer

# Restart service
Restart-Service -Name ColibriServer

# View status
Get-Service -Name ColibriServer

# View detailed status
Get-Service -Name ColibriServer | Select-Object *

# Set service to start automatically (default)
Set-Service -Name ColibriServer -StartupType Automatic

# Set service to start manually
Set-Service -Name ColibriServer -StartupType Manual

# Disable service
Set-Service -Name ColibriServer -StartupType Disabled
```

### Using Services GUI

1. Press `Win + R` and type `services.msc`
2. Find "Colibri Server" in the list
3. Right-click for Start/Stop/Restart options
4. Double-click to configure startup type

### Using Command Prompt

```cmd
REM Start service
net start ColibriServer

REM Stop service
net stop ColibriServer

REM View status
sc query ColibriServer
```

## View Logs

### Event Viewer

1. Press `Win + R` and type `eventvwr.msc`
2. Navigate to **Windows Logs** → **Application**
3. Filter by source: **ColibriServer**

### PowerShell

```powershell
# View last 50 events
Get-EventLog -LogName Application -Source ColibriServer -Newest 50

# View only error events
Get-EventLog -LogName Application -Source ColibriServer -EntryType Error -Newest 20

# Export logs to file
Get-EventLog -LogName Application -Source ColibriServer -Newest 100 | Out-File colibri-logs.txt
```

### Log Files

Check the log directory for detailed logs:

```powershell
Get-Content C:\ProgramData\Colibri\logs\server.log -Tail 50
```

## Firewall Configuration

### Check Firewall Rule

```powershell
# View Colibri firewall rules
Get-NetFirewallRule -DisplayName "Colibri Server*" | Format-Table
```

### Manually Add Firewall Rule

If the installer didn't create the firewall rule:

```powershell
New-NetFirewallRule -DisplayName "Colibri Server" `
  -Direction Inbound `
  -Protocol TCP `
  -LocalPort 8090 `
  -Action Allow `
  -Description "Allow inbound connections to Colibri Server"
```

### Remove Firewall Rule

```powershell
Remove-NetFirewallRule -DisplayName "Colibri Server"
```

## Uninstallation

### Using GUI

1. Open **Settings** → **Apps** → **Apps & features**
2. Search for "Colibri Server"
3. Click and select **Uninstall**
4. Follow the uninstallation wizard
5. Choose whether to keep configuration files

### Using PowerShell

```powershell
# Find the product code
$app = Get-WmiObject -Class Win32_Product | Where-Object { $_.Name -like "*Colibri*" }
$app | Select-Object Name, IdentifyingNumber

# Uninstall silently
msiexec /x {PRODUCT-GUID} /qn

# Or uninstall with GUI
msiexec /x {PRODUCT-GUID}
```

### Using MSI File

```powershell
msiexec /x colibri-server-1.0.0.msi
```

### Manual Cleanup (if needed)

If you want to remove all traces after uninstallation:

```powershell
# Stop and remove service (if still exists)
Stop-Service -Name ColibriServer -ErrorAction SilentlyContinue
sc.exe delete ColibriServer

# Remove installation directory
Remove-Item -Path "C:\Program Files\Colibri" -Recurse -Force

# Remove configuration and data
Remove-Item -Path "C:\ProgramData\Colibri" -Recurse -Force

# Remove firewall rule
Remove-NetFirewallRule -DisplayName "Colibri Server*"
```

## Troubleshooting

### Service Won't Start

1. **Check Event Viewer logs:**
   ```powershell
   Get-EventLog -LogName Application -Source ColibriServer -EntryType Error -Newest 10
   ```

2. **Common issues:**
   - Port 8090 already in use → Change `PORT` in config file
   - Invalid RPC endpoints → Check network connectivity
   - Config file syntax error → Validate config file format
   - Permission issues → Ensure service has necessary permissions

3. **Test manually:**
   ```powershell
   & "C:\Program Files\Colibri\colibri-server.exe" -c "C:\ProgramData\Colibri\server.conf"
   ```

### Port Already in Use

Check what's using port 8090:

```powershell
# Find process using port 8090
Get-NetTCPConnection -LocalPort 8090 | Select-Object OwningProcess
Get-Process -Id <ProcessId>
```

Either stop that process or change the port in `C:\ProgramData\Colibri\server.conf`.

### Permission Denied Errors

Ensure the service has read access to configuration:

```powershell
# Grant read permissions to LocalService
$acl = Get-Acl "C:\ProgramData\Colibri\server.conf"
$rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
    "NT AUTHORITY\LOCAL SERVICE", "Read", "Allow"
)
$acl.SetAccessRule($rule)
Set-Acl "C:\ProgramData\Colibri\server.conf" $acl
```

### Service Crashes Immediately

Check for missing DLL dependencies:

```powershell
# Download and install Visual C++ Redistributable if needed
# https://aka.ms/vs/17/release/vc_redist.x64.exe
```

### Firewall Blocking Connections

Temporarily disable Windows Firewall to test:

```powershell
# Disable (test only!)
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False

# Re-enable
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled True
```

If it works with firewall disabled, add the rule manually (see Firewall Configuration section).

## Building from Source

If you want to build the installer yourself:

```powershell
cd installer\windows
.\build_installer.ps1
```

Requirements:
- Visual Studio 2022 (or newer) with C++ build tools
- WiX Toolset 3.11+ (https://wixtoolset.org/)
- CMake (https://cmake.org/)

Output: `..\..build\colibri-server-1.0.0.msi`

## Support

- **Documentation**: https://corpus-core.gitbook.io/specification-colibri-stateless
- **Issues**: https://github.com/corpus-core/colibri-stateless/issues
- **Email**: jork@corpus.io

## License

The Colibri core library is licensed under the MIT License.

The server component is dual-licensed:
- **PolyForm Noncommercial License 1.0.0** for non-commercial use
- **Commercial License** required for commercial use (contact jork@corpus.io)

