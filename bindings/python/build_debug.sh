#!/bin/bash

# Debug build script for Colibri Python bindings

set -e

PROJECT_ROOT="$(cd ../.. && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/debug"
BINDINGS_DIR="$(pwd)"

echo "🔧 Building Colibri Python bindings in DEBUG mode..."
echo "   Project root: $PROJECT_ROOT"
echo "   Build dir: $BUILD_DIR"
echo "   Bindings dir: $BINDINGS_DIR"

# Check for pybind11 before building
PYTHON=${PYTHON:-python3}
echo "🐍 Checking Python and pybind11..."

if ! command -v "$PYTHON" &> /dev/null; then
    echo "❌ Python executable '$PYTHON' not found"
    exit 1
fi

if ! $PYTHON -c "import pybind11" &> /dev/null; then
    echo "⚠️ pybind11 not found, attempting to install..."
    
    # Try different installation methods
    if $PYTHON -m pip install pybind11 &> /dev/null; then
        echo "✅ Installed pybind11 via pip"
    elif $PYTHON -m pip install --user pybind11 &> /dev/null; then
        echo "✅ Installed pybind11 via pip --user"
    elif [[ "$(uname)" == "Darwin" ]] && command -v brew &> /dev/null; then
        echo "🍺 Attempting to install pybind11 via Homebrew..."
        if brew install pybind11 &> /dev/null; then
            echo "✅ Installed pybind11 via Homebrew"
        fi
    fi
    
    # Verify installation
    if ! $PYTHON -c "import pybind11" &> /dev/null; then
        echo "❌ Failed to install pybind11"
        echo ""
        echo "Please install pybind11 manually using one of these methods:"
        echo "  1. Using Homebrew (macOS):     brew install pybind11"
        echo "  2. Using pip with --user:      $PYTHON -m pip install --user pybind11"
        echo "  3. Using virtual environment:  python3 -m venv venv && source venv/bin/activate && pip install pybind11"
        exit 1
    fi
fi

PYBIND11_VERSION=$($PYTHON -c "import pybind11; print(pybind11.__version__)")
echo "✅ Using pybind11 $PYBIND11_VERSION"

# Clean previous build
echo "🧹 Cleaning previous build..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure with debug flags
echo "⚙️ Configuring CMake for DEBUG build..."
cd "$BUILD_DIR"

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DC4_PYTHON=ON \
    -DCURL=OFF \
    -DCLI=OFF \
    -DCMAKE_C_FLAGS_DEBUG="-g3 -O0 -fno-omit-frame-pointer -DDEBUG" \
    -DCMAKE_CXX_FLAGS_DEBUG="-g3 -O0 -fno-omit-frame-pointer -DDEBUG" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$PROJECT_ROOT"

# Build the Python extension
echo "🔨 Building _native Python extension..."
make -j4 _native

echo "✅ Debug build completed!"
echo ""
echo "🔍 Debug information:"
echo "   Extension module: $(find $BUILD_DIR -name '_native*.so' | head -1)"
echo "   Build type: $(cmake -LA -N $BUILD_DIR | grep CMAKE_BUILD_TYPE)"

# Copy debug symbols (if available)
NATIVE_SO=$(find "$BUILD_DIR" -name '_native*.so' | head -1)
if [ -f "$NATIVE_SO" ]; then
    echo "   Module size: $(du -h $NATIVE_SO | cut -f1)"
    echo "   Debug info: $(file $NATIVE_SO)"
    
    # Copy to bindings directory for Python import
    echo "📋 Copying debug module to Python package..."
    cp "$NATIVE_SO" "$BINDINGS_DIR/src/colibri/"
    echo "   Copied to: $BINDINGS_DIR/src/colibri/"
fi

# Copy compile_commands.json for VS Code IntelliSense
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "📋 Copying compile_commands.json for VS Code..."
    cp "$BUILD_DIR/compile_commands.json" "$PROJECT_ROOT/"
fi

echo ""
echo "🎯 Ready for debugging!"
echo "   1. Open VS Code in the project root"
echo "   2. Set breakpoints in C/C++ source files"
echo "   3. Run 'Debug Python with C++ (lldb)' configuration"
echo ""
echo "💡 Debugging tips:"
echo "   - Set breakpoints in src/bindings.cpp (storage callbacks)"
echo "   - Set breakpoints in storage_get_callback, storage_set_callback"
echo "   - Use 'scripts/debug_simple.py' for focused debugging"