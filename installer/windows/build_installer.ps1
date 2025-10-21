# Build Windows MSI installer for Colibri Server
# Requires: WiX Toolset (https://wixtoolset.org/)

param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$Version = "1.0.0"

Write-Host "Building Colibri Server Windows installer..." -ForegroundColor Green
Write-Host "Project root: $ProjectRoot"

# Check if WiX is installed
$wixPath = "${env:ProgramFiles(x86)}\WiX Toolset v3.11\bin"
if (-not (Test-Path "$wixPath\candle.exe")) {
    $wixPath = "${env:ProgramFiles}\WiX Toolset v3.11\bin"
}
if (-not (Test-Path "$wixPath\candle.exe")) {
    Write-Host "ERROR: WiX Toolset not found!" -ForegroundColor Red
    Write-Host "Download from: https://wixtoolset.org/releases/"
    exit 1
}

# Build the server binary
Write-Host "Building server binary..." -ForegroundColor Cyan
$BuildDir = "$ProjectRoot\build\windows-release"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Push-Location $BuildDir
try {
    cmake -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_BUILD_TYPE=$Configuration `
        -DHTTP_SERVER=ON `
        -DPROVER=ON `
        -DVERIFIER=ON `
        -DPROVER_CACHE=ON `
        -DCLI=OFF `
        -DTEST=OFF `
        -DINSTALLER=ON `
        "$ProjectRoot"
    
    cmake --build . --config $Configuration --target server -j 4
    
    if (-not $?) {
        throw "Build failed"
    }
} finally {
    Pop-Location
}

# Verify binary exists
$ServerExe = "$BuildDir\default\bin\$Configuration\server.exe"
if (-not (Test-Path $ServerExe)) {
    Write-Host "ERROR: Server binary not found at: $ServerExe" -ForegroundColor Red
    exit 1
}

# Build the installer
Write-Host "Building MSI installer..." -ForegroundColor Cyan
$InstallerDir = "$ProjectRoot\installer\windows"
$OutputDir = "$ProjectRoot\build"

Push-Location $InstallerDir
try {
    # Compile WiX source
    & "$wixPath\candle.exe" -nologo -arch x64 `
        -dConfiguration=$Configuration `
        -dProjectRoot=$ProjectRoot `
        colibri.wxs
    
    if (-not $?) {
        throw "WiX compilation failed"
    }
    
    # Link to create MSI
    & "$wixPath\light.exe" -nologo `
        -ext WixUIExtension `
        -ext WixUtilExtension `
        -cultures:en-US `
        -out "$OutputDir\colibri-server-$Version.msi" `
        colibri.wixobj
    
    if (-not $?) {
        throw "WiX linking failed"
    }
    
    # Clean up temporary files
    Remove-Item *.wixobj -ErrorAction SilentlyContinue
    Remove-Item *.wixpdb -ErrorAction SilentlyContinue
    
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host "Windows installer built successfully!" -ForegroundColor Green
Write-Host "Installer: $OutputDir\colibri-server-$Version.msi" -ForegroundColor Yellow
Write-Host ""
Write-Host "To install: msiexec /i colibri-server-$Version.msi" -ForegroundColor Cyan
Write-Host "Or: Double-click the MSI file" -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Green

