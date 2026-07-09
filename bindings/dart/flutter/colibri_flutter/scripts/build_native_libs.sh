#!/usr/bin/env bash
# Build native Colibri libraries for the Flutter plugin.
#
# Usage:
#   ./scripts/build_native_libs.sh --android   # Android only (requires ANDROID_NDK_HOME)
#   ./scripts/build_native_libs.sh --ios       # iOS only (requires macOS + Xcode)
#   ./scripts/build_native_libs.sh --all       # Both platforms
#   ./scripts/build_native_libs.sh             # Auto-detect: iOS on macOS, Android if NDK present
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$PLUGIN_DIR/../../../.." && pwd)"

ANDROID_ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
ANDROID_API_LEVEL=23
JNILIBS_DIR="$PLUGIN_DIR/android/src/main/jniLibs"
IOS_FRAMEWORKS_DIR="$PLUGIN_DIR/ios/Frameworks"

build_android=false
build_ios=false
build_macos=false
build_windows=false

parse_args() {
    if [[ $# -eq 0 ]]; then
        # Auto-detect
        if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
            build_android=true
        fi
        if [[ "$(uname)" == "Darwin" ]]; then
            build_ios=true
            build_macos=true
        fi
        if ! $build_android && ! $build_ios && ! $build_macos; then
            echo "Error: No platform available."
            echo "  Android: set ANDROID_NDK_HOME"
            echo "  iOS/macOS: run on macOS with Xcode"
            exit 1
        fi
        return
    fi

    for arg in "$@"; do
        case "$arg" in
            --android)  build_android=true ;;
            --ios)      build_ios=true ;;
            --macos)    build_macos=true ;;
            --windows)  build_windows=true ;;
            --all)      build_android=true; build_ios=true; build_macos=true; build_windows=true ;;
            *)
                echo "Unknown option: $arg"
                echo "Usage: $0 [--android] [--ios] [--macos] [--windows] [--all]"
                exit 1
                ;;
        esac
    done
}

build_android_libs() {
    if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
        echo "Error: ANDROID_NDK_HOME is not set."
        echo "Install the Android NDK and export ANDROID_NDK_HOME."
        exit 1
    fi

    local toolchain="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    if [[ ! -f "$toolchain" ]]; then
        echo "Error: Android NDK toolchain not found at $toolchain"
        exit 1
    fi

    echo "=== Building Android native libraries ==="
    echo "NDK: $ANDROID_NDK_HOME"
    echo "ABIs: ${ANDROID_ABIS[*]}"

    for abi in "${ANDROID_ABIS[@]}"; do
        echo ""
        echo "--- Building for $abi ---"
        local build_dir="$ROOT_DIR/build/flutter-android-$abi"

        cmake -S "$ROOT_DIR" -B "$build_dir" \
            -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
            -DANDROID_ABI="$abi" \
            -DANDROID_PLATFORM="android-$ANDROID_API_LEVEL" \
            -DANDROID_STL=c++_static \
            -DANDROID_TOOLCHAIN=clang \
            -DDART=ON \
            -DETH_ZKPROOF=true \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_STANDARD=20 \
            -DCMAKE_CXX_STANDARD_REQUIRED=ON

        cmake --build "$build_dir" --target colibri_dart -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

        local so_file="$build_dir/bindings/dart/libcolibri.so"
        if [[ ! -f "$so_file" ]]; then
            so_file=$(find "$build_dir" -name "libcolibri.so" -print -quit 2>/dev/null || true)
        fi

        if [[ -z "$so_file" || ! -f "$so_file" ]]; then
            echo "Error: libcolibri.so not found for $abi"
            exit 1
        fi

        mkdir -p "$JNILIBS_DIR/$abi"
        cp "$so_file" "$JNILIBS_DIR/$abi/libcolibri.so"
        echo "Copied: $JNILIBS_DIR/$abi/libcolibri.so ($(du -h "$JNILIBS_DIR/$abi/libcolibri.so" | cut -f1))"
    done

    echo ""
    echo "=== Android build complete ==="
    ls -lhR "$JNILIBS_DIR"
}

build_ios_libs() {
    if [[ "$(uname)" != "Darwin" ]]; then
        echo "Error: iOS build requires macOS."
        exit 1
    fi

    local ios_build_script="$ROOT_DIR/bindings/swift/build_ios.sh"
    if [[ ! -f "$ios_build_script" ]]; then
        echo "Error: iOS build script not found at $ios_build_script"
        exit 1
    fi

    echo "=== Building iOS XCFramework ==="

    bash "$ios_build_script"

    local xcframework="$ROOT_DIR/build/ios/ios_arm64/c4_swift.xcframework"
    if [[ ! -d "$xcframework" ]]; then
        echo "Error: XCFramework not found at $xcframework"
        exit 1
    fi

    mkdir -p "$IOS_FRAMEWORKS_DIR"
    rm -rf "$IOS_FRAMEWORKS_DIR/c4_swift.xcframework"
    cp -R "$xcframework" "$IOS_FRAMEWORKS_DIR/c4_swift.xcframework"

    echo ""
    echo "=== iOS build complete ==="
    echo "Copied: $IOS_FRAMEWORKS_DIR/c4_swift.xcframework"
    du -sh "$IOS_FRAMEWORKS_DIR/c4_swift.xcframework"
}

build_macos_libs() {
    if [[ "$(uname)" != "Darwin" ]]; then
        echo "Error: macOS build requires macOS."
        exit 1
    fi

    echo "=== Building macOS universal dylib ==="
    local dylibs=()
    for arch in arm64 x86_64; do
        echo "--- Building macOS $arch ---"
        local build_dir="$ROOT_DIR/build/flutter-macos-$arch"

        cmake -S "$ROOT_DIR" -B "$build_dir" \
            -DDART=ON \
            -DETH_ZKPROOF=ON \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES="$arch"

        cmake --build "$build_dir" --target colibri_dart -j "$(sysctl -n hw.ncpu)"

        local dylib
        dylib=$(find "$build_dir" -name "libcolibri.dylib" -print -quit)
        dylibs+=("$dylib")
    done

    local dest_dir="$PLUGIN_DIR/macos/Frameworks"
    mkdir -p "$dest_dir"
    lipo -create "${dylibs[@]}" -output "$dest_dir/libcolibri.dylib"

    echo ""
    echo "=== macOS build complete ==="
    echo "Universal dylib: $dest_dir/libcolibri.dylib ($(du -h "$dest_dir/libcolibri.dylib" | cut -f1))"
}

build_windows_libs() {
    echo "=== Building Windows DLL ==="
    local build_dir="$ROOT_DIR/build/flutter-windows"

    cmake -S "$ROOT_DIR" -B "$build_dir" \
        -DDART=ON \
        -DETH_ZKPROOF=ON \
        -DCMAKE_BUILD_TYPE=Release

    cmake --build "$build_dir" --config Release --target colibri_dart

    local dll
    dll=$(find "$build_dir" -name "colibri.dll" -print -quit 2>/dev/null || true)
    if [[ -z "$dll" ]]; then
        dll=$(find "$build_dir" -name "libcolibri.dll" -print -quit 2>/dev/null || true)
    fi
    if [[ -z "$dll" || ! -f "$dll" ]]; then
        echo "Error: colibri.dll not found"
        exit 1
    fi

    local dest_dir="$PLUGIN_DIR/windows/lib"
    mkdir -p "$dest_dir"
    cp "$dll" "$dest_dir/colibri.dll"

    echo ""
    echo "=== Windows build complete ==="
    echo "DLL: $dest_dir/colibri.dll ($(du -h "$dest_dir/colibri.dll" | cut -f1))"
}

parse_args "$@"

if $build_android; then
    build_android_libs
fi

if $build_ios; then
    build_ios_libs
fi

if $build_macos; then
    build_macos_libs
fi

if $build_windows; then
    build_windows_libs
fi

echo ""
echo "Done. Native libraries are ready for Flutter plugin publishing."
