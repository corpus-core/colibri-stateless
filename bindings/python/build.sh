#!/bin/bash

# Colibri Python Bindings Build Script
# Copyright (c) 2025 corpus.core
# SPDX-License-Identifier: MIT

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Configuration
DEBUG=false
CLEAN=false
INSTALL=false
VERBOSE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Build Colibri Python bindings"
    echo ""
    echo "OPTIONS:"
    echo "  --debug       Build in debug mode"
    echo "  --clean       Clean build directory before building"
    echo "  --install     Install the package after building"
    echo "  --verbose     Verbose output"
    echo "  -h, --help    Show this help message"
    echo ""
    echo "ENVIRONMENT VARIABLES:"
    echo "  PYTHON        Python executable to use (default: python3)"
    echo "  CMAKE_ARGS    Additional CMake arguments"
    echo "  BUILD_JOBS    Number of parallel build jobs (default: auto-detect)"
}

log() {
    echo -e "${BLUE}[BUILD]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            DEBUG=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --install)
            INSTALL=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Set Python executable
PYTHON=${PYTHON:-python3}

# Verify Python availability
if ! command -v "$PYTHON" &> /dev/null; then
    log_error "Python executable '$PYTHON' not found"
    exit 1
fi

PYTHON_VERSION=$($PYTHON --version 2>&1 | cut -d' ' -f2 | cut -d'.' -f1,2)
log "Using Python $PYTHON_VERSION at $(which $PYTHON)"

# Check Python version (minimum 3.8)
PYTHON_MAJOR=$($PYTHON -c "import sys; print(sys.version_info.major)")
PYTHON_MINOR=$($PYTHON -c "import sys; print(sys.version_info.minor)")

if [[ $PYTHON_MAJOR -lt 3 ]] || [[ $PYTHON_MAJOR -eq 3 && $PYTHON_MINOR -lt 8 ]]; then
    log_error "Python 3.8 or newer is required (found $PYTHON_VERSION)"
    exit 1
fi

# Detect number of CPU cores for parallel building
if [[ -z "$BUILD_JOBS" ]]; then
    if command -v nproc &> /dev/null; then
        BUILD_JOBS=$(nproc)
    elif [[ "$(uname)" == "Darwin" ]]; then
        BUILD_JOBS=$(sysctl -n hw.ncpu)
    else
        BUILD_JOBS=4
    fi
fi

log "Using $BUILD_JOBS parallel build jobs"

# Check required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        log_error "$1 is required but not installed"
        return 1
    fi
}

log "Checking required tools..."
check_tool cmake || exit 1
check_tool make || exit 1

# Check CMake version (minimum 3.20)
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d'.' -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d'.' -f2)

if [[ $CMAKE_MAJOR -lt 3 ]] || [[ $CMAKE_MAJOR -eq 3 && $CMAKE_MINOR -lt 20 ]]; then
    log_error "CMake 3.20 or newer is required (found $CMAKE_VERSION)"
    exit 1
fi

log "Using CMake $CMAKE_VERSION"

# Check if pybind11 is available
log "Checking pybind11 availability..."
if ! $PYTHON -c "import pybind11" &> /dev/null; then
    log_warning "pybind11 not found, attempting to install..."
    $PYTHON -m pip install pybind11
    if ! $PYTHON -c "import pybind11" &> /dev/null; then
        log_error "Failed to install or import pybind11"
        exit 1
    fi
fi

PYBIND11_VERSION=$($PYTHON -c "import pybind11; print(pybind11.__version__)")
log "Using pybind11 $PYBIND11_VERSION"

# Clean build directory if requested
if [[ "$CLEAN" == "true" ]]; then
    log "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    # Also clean Python build artifacts
    cd "$SCRIPT_DIR"
    rm -rf build/ dist/ *.egg-info/ src/colibri/_native.* src/colibri/*.so
fi

# Build with CMake using PYTHON=ON
log "Building Colibri with Python bindings..."
cd "$PROJECT_ROOT"

if [[ ! -d "$BUILD_DIR" ]]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure CMake with Python bindings enabled
CMAKE_BUILD_TYPE="Release"
if [[ "$DEBUG" == "true" ]]; then
    CMAKE_BUILD_TYPE="Debug"
fi

CMAKE_ARGS_ARRAY=(
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DPYTHON=ON
    $CMAKE_ARGS
)

if [[ "$VERBOSE" == "true" ]]; then
    CMAKE_ARGS_ARRAY+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
fi

log "Configuring with CMake..."
cmake "${CMAKE_ARGS_ARRAY[@]}" ..

log "Building..."
make -j"$BUILD_JOBS"

# Note: We no longer build a shared library since all dependencies 
# are statically linked into the Python module

# Verify that the Python extension was built
NATIVE_MODULE="$SCRIPT_DIR/src/colibri/_native"*
if ! ls $NATIVE_MODULE 1> /dev/null 2>&1; then
    log_error "Python extension module not found at $SCRIPT_DIR/src/colibri/"
    exit 1
fi

log_success "Build completed successfully!"

# Install package if requested
if [[ "$INSTALL" == "true" ]]; then
    log "Installing Python package..."
    cd "$SCRIPT_DIR"
    
    # Install in development mode for easier development
    $PYTHON -m pip install -e .
    
    log_success "Python package installed successfully"
fi

# Test Python module import
log "Testing Python module import..."
cd "$SCRIPT_DIR"

if $PYTHON -c "import colibri; print(f'Colibri {colibri.__version__} imported successfully')" 2>/dev/null; then
    log_success "Python module import test passed"
else
    log_warning "Python module import test failed - this might be expected if dependencies are missing"
fi

# Summary
echo ""
log_success "Build completed successfully!"
echo ""
echo "Files created:"
echo "  Python extension: $(ls $NATIVE_MODULE | head -n 1) (with all dependencies statically linked)"
echo ""
echo "Next steps:"
echo "  1. Install the package: $PYTHON -m pip install -e $SCRIPT_DIR"
echo "  2. Run tests: $PYTHON -m pytest $SCRIPT_DIR/tests/"
echo "  3. Import in Python: import colibri"
echo ""

if [[ "$DEBUG" == "true" ]]; then
    echo "Debug build artifacts:"
    echo "  Build directory: $BUILD_DIR"
    echo ""
fi