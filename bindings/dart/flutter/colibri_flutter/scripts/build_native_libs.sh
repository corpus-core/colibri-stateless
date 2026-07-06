#!/usr/bin/env bash
# Build native Colibri libraries for the Flutter plugin.
#
# Usage:
#   ./scripts/build_native_libs.sh --android   # Android only (requires ANDROID_NDK_HOME)
#   ./scripts/build_native_libs.sh --ios       # iOS only (requires macOS + Xcode)
#   ./scripts/build_native_libs.sh --macos     # macOS dylib + xcframework (requires macOS; host arch)
#   ./scripts/build_native_libs.sh --linux     # Linux .so (requires Linux host)
#   ./scripts/build_native_libs.sh --all       # All platforms available on this host
#   ./scripts/build_native_libs.sh             # Auto-detect: iOS + macOS on macOS, Android if NDK present
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$PLUGIN_DIR/../../../.." && pwd)"

# ZK sync verification uses SP1 v6; the prover only serves v6 proofs to clients that
# report version >= 2.0.0 (C4_ZK_FIRST_V6_VERSION). The v6-only verifier cannot check
# the legacy v5 proof an older client would receive ("VK not found" -> "invalid
# zk_proof!"). The repo git tag is still 1.x, so force a v6-capable client version for
# all bundled native binaries (android/ios/desktop) unless one is explicitly provided.
# Exported so it propagates to the child cmake/build scripts.
if [[ -z "${C4_VERSION:-}" ]]; then
  _gitv="$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || true)"
  _major="$(printf '%s' "${_gitv#v}" | cut -d. -f1)"
  if [[ "$_major" =~ ^[0-9]+$ ]] && ((_major >= 2)); then
    export C4_VERSION="$_gitv"
  else
    export C4_VERSION="2.0.0"
  fi
fi
echo "Using C4_VERSION=$C4_VERSION (ZK SP1 v6 requires client >= 2.0.0)"

ANDROID_ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
ANDROID_API_LEVEL=23
JNILIBS_DIR="$PLUGIN_DIR/android/src/main/jniLibs"
IOS_FRAMEWORKS_DIR="$PLUGIN_DIR/ios/Frameworks"

# Use ANDROID_NDK_HOME, or the newest NDK under Android SDK (common on macOS).
resolve_android_ndk() {
  if [[ -n "${ANDROID_NDK_HOME:-}" && -d "$ANDROID_NDK_HOME" ]]; then
    return 0
  fi
  local sdk="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
  if [[ ! -d "$sdk/ndk" ]]; then
    return 0
  fi
  local latest
  latest="$(ls -1 "$sdk/ndk" 2>/dev/null | sort -V | tail -1 || true)"
  if [[ -n "$latest" && -d "$sdk/ndk/$latest" ]]; then
    export ANDROID_NDK_HOME="$sdk/ndk/$latest"
  fi
}

resolve_android_ndk

build_android=false
build_ios=false
build_macos=false
build_linux=false

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
        if [[ "$(uname)" == "Linux" ]]; then
            build_linux=true
        fi
        if ! $build_android && ! $build_ios && ! $build_macos && ! $build_linux; then
            echo "Error: No platform available."
            echo "  Android: set ANDROID_NDK_HOME"
            echo "  iOS/macOS: run on macOS with Xcode"
            echo "  Linux: run on a Linux host"
            exit 1
        fi
        return
    fi

    for arg in "$@"; do
        case "$arg" in
            --android) build_android=true ;;
            --ios)     build_ios=true ;;
            --macos)   build_macos=true ;;
            --linux)   build_linux=true ;;
            --all)
                build_android=true
                build_ios=true
                if [[ "$(uname)" == "Darwin" ]]; then
                    build_macos=true
                fi
                if [[ "$(uname)" == "Linux" ]]; then
                    build_linux=true
                fi
                ;;
            *)
                echo "Unknown option: $arg"
                echo "Usage: $0 [--android] [--ios] [--macos] [--linux] [--all]"
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

build_desktop_libs() {
    local flutter_bins="$ROOT_DIR/scripts/build_flutter_binaries.sh"
    if [[ ! -f "$flutter_bins" ]]; then
        echo "Error: build script not found at $flutter_bins"
        exit 1
    fi
    local flags=()
    if $build_macos; then
        flags+=(--macos)
    fi
    if $build_linux; then
        flags+=(--linux)
    fi
    echo "=== Building desktop native libraries (${flags[*]}) ==="
    bash "$flutter_bins" "${flags[@]}"
}

parse_args "$@"

if $build_android; then
    build_android_libs
fi

if $build_ios; then
    build_ios_libs
fi

if $build_macos || $build_linux; then
    build_desktop_libs
fi

echo ""
echo "Done. Native libraries are ready for Flutter plugin publishing."
