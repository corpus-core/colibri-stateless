# Install Colibri Server as Windows Service
# Alternative approach using NSSM (Non-Sucking Service Manager)

param(
    [string]$BinaryPath = "C:\Program Files\Colibri\colibri-server.exe",
    [string]$ConfigPath = "$env:ProgramData\Colibri\server.conf",
    [string]$ServiceName = "ColibriServer",
    [switch]$UseNSSM = $false
)

$ErrorActionPreference = "Stop"

Write-Host "Installing Colibri Server as Windows Service..." -ForegroundColor Green

# Check if service already exists
$existingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($existingService) {
    Write-Host "Service '$ServiceName' already exists. Stopping and removing..." -ForegroundColor Yellow
    Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    sc.exe delete $ServiceName
    Start-Sleep -Seconds 2
}

if ($UseNSSM) {
    # Using NSSM (recommended for easier management)
    Write-Host "Using NSSM to create service..." -ForegroundColor Cyan
    
    # Download NSSM if not present
    $nssmPath = "$env:TEMP\nssm.exe"
    if (-not (Test-Path $nssmPath)) {
        Write-Host "Downloading NSSM..."
        $nssmUrl = "https://nssm.cc/release/nssm-2.24.zip"
        $nssmZip = "$env:TEMP\nssm.zip"
        Invoke-WebRequest -Uri $nssmUrl -OutFile $nssmZip
        Expand-Archive -Path $nssmZip -DestinationPath "$env:TEMP\nssm" -Force
        Copy-Item "$env:TEMP\nssm\nssm-2.24\win64\nssm.exe" $nssmPath
        Remove-Item $nssmZip, "$env:TEMP\nssm" -Recurse -Force
    }
    
    # Install service with NSSM
    & $nssmPath install $ServiceName $BinaryPath "-c" $ConfigPath
    & $nssmPath set $ServiceName DisplayName "Colibri Stateless Server"
    & $nssmPath set $ServiceName Description "Ethereum proof generation and verification server"
    & $nssmPath set $ServiceName Start SERVICE_AUTO_START
    & $nssmPath set $ServiceName AppStdout "$env:ProgramData\Colibri\service.log"
    & $nssmPath set $ServiceName AppStderr "$env:ProgramData\Colibri\service-error.log"
    
} else {
    # Using built-in Windows sc.exe
    Write-Host "Using Windows Service Control Manager..." -ForegroundColor Cyan
    
    $binPathWithArgs = "`"$BinaryPath`" -c `"$ConfigPath`""
    sc.exe create $ServiceName binPath= $binPathWithArgs start= auto DisplayName= "Colibri Stateless Server"
    sc.exe description $ServiceName "Ethereum proof generation and verification server"
}

# Add firewall rule
Write-Host "Adding firewall rule..." -ForegroundColor Cyan
$portMatch = Select-String -Path $ConfigPath -Pattern "^PORT=(\d+)" | Select-Object -First 1
if ($portMatch) {
    $port = $portMatch.Matches.Groups[1].Value
    netsh advfirewall firewall add rule name="Colibri Server" dir=in action=allow protocol=TCP localport=$port
    Write-Host "Firewall rule added for port $port" -ForegroundColor Green
}

# Start the service
Write-Host "Starting service..." -ForegroundColor Cyan
Start-Service -Name $ServiceName

Write-Host ""
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host "Colibri Server service installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "Service Name: $ServiceName" -ForegroundColor Yellow
Write-Host "Configuration: $ConfigPath" -ForegroundColor Yellow
Write-Host ""
Write-Host "Manage the service:" -ForegroundColor Cyan
Write-Host "  Start:   Start-Service -Name $ServiceName" -ForegroundColor White
Write-Host "  Stop:    Stop-Service -Name $ServiceName" -ForegroundColor White
Write-Host "  Status:  Get-Service -Name $ServiceName" -ForegroundColor White
Write-Host "  Restart: Restart-Service -Name $ServiceName" -ForegroundColor White
Write-Host "=====================================================================" -ForegroundColor Green

