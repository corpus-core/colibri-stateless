# Build Windows MSI installer for Colibri Server
# Requires: WiX Toolset (https://wixtoolset.org/)

param(
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

Write-Host "Building Colibri Server Windows installer..." -ForegroundColor Green
Write-Host "Version: $Version"
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
    # Use CMAKE_TOOLCHAIN_FILE if set (for vcpkg)
    $cmakeArgs = @(
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DHTTP_SERVER=ON",
        "-DPROVER=ON",
        "-DVERIFIER=ON",
        "-DPROVER_CACHE=ON",
        "-DCLI=ON",
        "-DTEST=OFF",
        "-DINSTALLER=ON"
    )
    
    if ($env:CMAKE_TOOLCHAIN_FILE) {
        Write-Host "Using CMake toolchain file: $env:CMAKE_TOOLCHAIN_FILE" -ForegroundColor Green
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$env:CMAKE_TOOLCHAIN_FILE"
        
        # Also set vcpkg-specific paths if VCPKG_ROOT is available
        if ($env:VCPKG_ROOT) {
            Write-Host "Setting vcpkg paths from VCPKG_ROOT: $env:VCPKG_ROOT" -ForegroundColor Green
            $cmakeArgs += "-DCURL_INCLUDE_DIR=$env:VCPKG_ROOT\installed\x64-windows\include"
            $cmakeArgs += "-DCURL_LIBRARY=$env:VCPKG_ROOT\installed\x64-windows\lib\libcurl.lib"
        }
    }
    
    $cmakeArgs += "$ProjectRoot"
    
    & cmake @cmakeArgs
    
    # Build server and CLI tools
    cmake --build . --config $Configuration --target colibri-server -j 4
    cmake --build . --config $Configuration --target colibri-prover -j 4
    cmake --build . --config $Configuration --target colibri-verifier -j 4
    cmake --build . --config $Configuration --target colibri-ssz -j 4
    
    if (-not $?) {
        throw "Build failed"
    }
} finally {
    Pop-Location
}

# Verify binaries exist - try both possible locations
$ServerExe = "$BuildDir\bin\$Configuration\colibri-server.exe"
if (-not (Test-Path $ServerExe)) {
    # Try alternative path with 'default' subdirectory
    $ServerExe = "$BuildDir\default\bin\$Configuration\colibri-server.exe"
    if (-not (Test-Path $ServerExe)) {
        Write-Host "ERROR: Server binary not found at either:" -ForegroundColor Red
        Write-Host "  $BuildDir\bin\$Configuration\colibri-server.exe" -ForegroundColor Red
        Write-Host "  $BuildDir\default\bin\$Configuration\colibri-server.exe" -ForegroundColor Red
        exit 1
    }
}

# Verify CLI tools
$CliToolsDir = Split-Path $ServerExe -Parent
$ProverExe = Join-Path $CliToolsDir "colibri-prover.exe"
$VerifierExe = Join-Path $CliToolsDir "colibri-verifier.exe"
$SszExe = Join-Path $CliToolsDir "colibri-ssz.exe"

if (-not (Test-Path $ProverExe)) {
    Write-Host "WARNING: colibri-prover.exe not found at $ProverExe" -ForegroundColor Yellow
}
if (-not (Test-Path $VerifierExe)) {
    Write-Host "WARNING: colibri-verifier.exe not found at $VerifierExe" -ForegroundColor Yellow
}
if (-not (Test-Path $SszExe)) {
    Write-Host "WARNING: colibri-ssz.exe not found at $SszExe" -ForegroundColor Yellow
}

# Build the installer
Write-Host "Building MSI installer..." -ForegroundColor Cyan
$InstallerDir = "$ProjectRoot\installer\windows"
$OutputDir = $InstallerDir

Push-Location $InstallerDir
try {
    Write-Host "Using server binary: $ServerExe" -ForegroundColor Green
    Write-Host "Using CLI tools from: $CliToolsDir" -ForegroundColor Green
    
    # Compile WiX source
    & "$wixPath\candle.exe" -nologo -arch x64 `
        -dConfiguration=$Configuration `
        -dProjectRoot=$ProjectRoot `
        -dVersion=$Version `
        -dServerExePath="$ServerExe" `
        -dProverExePath="$ProverExe" `
        -dVerifierExePath="$VerifierExe" `
        -dSszExePath="$SszExe" `
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

