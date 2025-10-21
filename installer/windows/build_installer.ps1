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

# Check if WiX is installed - try multiple locations
$wixPath = $null

# Try candle.exe in PATH first
$candleInPath = Get-Command candle.exe -ErrorAction SilentlyContinue
if ($candleInPath) {
    $wixPath = Split-Path $candleInPath.Source
    Write-Host "Found WiX in PATH: $wixPath" -ForegroundColor Green
}

# Try common installation paths
if (-not $wixPath) {
    $searchPaths = @(
        "${env:ProgramFiles(x86)}\WiX Toolset v3.11\bin",
        "${env:ProgramFiles}\WiX Toolset v3.11\bin",
        "${env:ProgramFiles(x86)}\WiX Toolset v3.14\bin",
        "${env:ProgramFiles}\WiX Toolset v3.14\bin"
    )
    
    # Also search for any WiX version
    $wixDirs = Get-ChildItem "${env:ProgramFiles(x86)}\" -Filter "WiX*" -Directory -ErrorAction SilentlyContinue
    foreach ($dir in $wixDirs) {
        $searchPaths += Join-Path $dir.FullName "bin"
    }
    
    foreach ($path in $searchPaths) {
        if (Test-Path "$path\candle.exe") {
            $wixPath = $path
            Write-Host "Found WiX at: $wixPath" -ForegroundColor Green
            break
        }
    }
}

if (-not $wixPath) {
    Write-Host "ERROR: WiX Toolset not found!" -ForegroundColor Red
    Write-Host "Searched paths:" -ForegroundColor Yellow
    Write-Host "  - PATH environment variable" -ForegroundColor Yellow
    Write-Host "  - ${env:ProgramFiles(x86)}\WiX Toolset v*\bin" -ForegroundColor Yellow
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

