#!/usr/bin/env bash
# Build native Colibri libraries for the Flutter plugin.
#
# Usage:
#   ./scripts/build_native_libs.sh --android   # Android only (requires ANDROID_NDK_HOME)
#   ./scripts/build_native_libs.sh --ios       # iOS only (requires macOS + Xcode)
#   ./scripts/build_native_libs.sh --macos     # macOS universal libcolibri.dylib
#   ./scripts/build_native_libs.sh --all       # Android, iOS, and macOS
#   ./scripts/build_native_libs.sh             # Auto-detect: iOS + macOS on macOS, Android if NDK present
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$PLUGIN_DIR/../../../.." && pwd)"

ANDROID_ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
ANDROID_API_LEVEL=23
JNILIBS_DIR="$PLUGIN_DIR/android/src/main/jniLibs"
IOS_FRAMEWORKS_DIR="$PLUGIN_DIR/ios/colibri_flutter/Frameworks"

build_android=false
build_ios=false
build_macos=false

# Resolve ANDROID_NDK_HOME from env or the Android SDK (same logic as
# scripts/build_flutter_binaries.sh). Prints the chosen path to stdout when found.
resolve_android_ndk() {
    local ndk_root="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"
    if [[ -n "$ndk_root" && -d "$ndk_root" ]]; then
        printf '%s' "$ndk_root"
        return 0
    fi

    local sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
    if [[ -z "$sdk_root" ]]; then
        if [[ "$(uname)" == "Darwin" ]]; then
            sdk_root="$HOME/Library/Android/sdk"
        else
            sdk_root="${HOME}/Android/Sdk"
        fi
    fi

    if [[ -n "$sdk_root" && -d "$sdk_root/ndk" ]]; then
        local latest_ndk
        latest_ndk="$(ls -1 "$sdk_root/ndk" 2>/dev/null | sort -V | tail -n 1 || true)"
        if [[ -n "$latest_ndk" && -d "$sdk_root/ndk/$latest_ndk" ]]; then
            printf '%s' "$sdk_root/ndk/$latest_ndk"
            return 0
        fi
    fi
    if [[ -n "$sdk_root" && -d "$sdk_root/ndk-bundle" ]]; then
        printf '%s' "$sdk_root/ndk-bundle"
        return 0
    fi
    return 1
}

parse_args() {
    if [[ $# -eq 0 ]]; then
        # Auto-detect
        if resolve_android_ndk >/dev/null; then
            build_android=true
        fi
        if [[ "$(uname)" == "Darwin" ]]; then
            build_ios=true
            build_macos=true
        fi
        if ! $build_android && ! $build_ios && ! $build_macos; then
            echo "Error: No platform available."
            echo "  Android: install NDK under \$ANDROID_HOME/ndk or set ANDROID_NDK_HOME"
            echo "  iOS: run on macOS with Xcode"
            exit 1
        fi
        return
    fi

    for arg in "$@"; do
        case "$arg" in
            --android) build_android=true ;;
            --ios)     build_ios=true ;;
            --macos)   build_macos=true ;;
            --all)     build_android=true; build_ios=true; build_macos=true ;;
            *)
                echo "Unknown option: $arg"
                echo "Usage: $0 [--android] [--ios] [--macos] [--all]"
                exit 1
                ;;
        esac
    done
}

build_android_libs() {
    local ndk_root
    if ! ndk_root="$(resolve_android_ndk)"; then
        echo "Error: Android NDK not found."
        echo "Install the Android NDK (Android Studio SDK Manager) or export ANDROID_NDK_HOME."
        echo "Default lookup: \$ANDROID_HOME/ndk/<version> or ~/Library/Android/sdk/ndk/<version>"
        exit 1
    fi
    export ANDROID_NDK_HOME="$ndk_root"
    export ANDROID_NDK="$ndk_root"

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

parse_args "$@"

if $build_android; then
    build_android_libs
fi

if $build_ios; then
    build_ios_libs
fi

if $build_macos; then
    if [[ "$(uname)" != "Darwin" ]]; then
        echo "Error: macOS build requires macOS."
        exit 1
    fi
    echo "=== Building macOS libcolibri.dylib ==="
    "$ROOT_DIR/scripts/build_flutter_binaries.sh" --macos
fi

echo ""
echo "Done. Native libraries are ready for Flutter plugin publishing."
