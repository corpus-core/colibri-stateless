#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DART_NATIVE_DIR="$ROOT_DIR/bindings/dart/native"
BUILD_ROOT="$ROOT_DIR/build/flutter"
FLUTTER_PLUGIN_DIR="$ROOT_DIR/bindings/dart/flutter/colibri_flutter"

mkdir -p "$BUILD_ROOT"

print_usage() {
  echo "Usage: $0 [--android] [--ios] [--macos] [--linux] [--windows] [--all]"
  echo ""
  echo "Build platform-specific binaries for Flutter/Dart FFI."
  echo "Defaults to --all when no flags are provided."
}

want_android=false
want_ios=false
want_macos=false
want_linux=false
want_windows=false

if [[ $# -eq 0 ]]; then
  want_android=true
  want_ios=true
  want_macos=true
  want_linux=true
  want_windows=true
else
  for arg in "$@"; do
    case "$arg" in
      --android) want_android=true ;;
      --ios) want_ios=true ;;
      --macos) want_macos=true ;;
      --linux) want_linux=true ;;
      --windows) want_windows=true ;;
      --all) want_android=true; want_ios=true; want_macos=true; want_linux=true; want_windows=true ;;
      -h|--help) print_usage; exit 0 ;;
      *) echo "Unknown argument: $arg"; print_usage; exit 1 ;;
    esac
  done
fi

host_os="$(uname -s)"
host_arch="$(uname -m)"

android_host_tag() {
  case "$host_os" in
    Darwin)
      if [[ "$host_arch" == "arm64" ]]; then
        echo "darwin-arm64"
      else
        echo "darwin-x86_64"
      fi
      ;;
    Linux)
      echo "linux-x86_64"
      ;;
    *)
      echo ""
      ;;
  esac
}

build_android() {
  local ndk_root="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"
  if [[ -z "$ndk_root" ]]; then
    local sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
    if [[ -z "$sdk_root" ]]; then
      if [[ "$host_os" == "Darwin" ]]; then
        sdk_root="$HOME/Library/Android/sdk"
      else
        sdk_root="${HOME}/Android/Sdk"
      fi
    fi
    if [[ -n "$sdk_root" ]] && [[ -d "$sdk_root" ]]; then
      if [[ -d "$sdk_root/ndk" ]]; then
        local latest_ndk
        latest_ndk="$(ls -1 "$sdk_root/ndk" 2>/dev/null | sort -V | tail -n 1 || true)"
        if [[ -n "$latest_ndk" ]]; then
          ndk_root="$sdk_root/ndk/$latest_ndk"
          echo "Using Android NDK (from SDK): $ndk_root"
        fi
      fi
      if [[ -z "$ndk_root" ]] && [[ -d "$sdk_root/ndk-bundle" ]]; then
        ndk_root="$sdk_root/ndk-bundle"
        echo "Using Android NDK (ndk-bundle): $ndk_root"
      fi
    fi
  else
    echo "Using Android NDK (ANDROID_NDK_HOME/ANDROID_NDK): $ndk_root"
  fi
  if [[ -z "$ndk_root" ]]; then
    echo "Android NDK not found. Set ANDROID_NDK_HOME or install NDK via Android Studio (default: $HOME/Library/Android/sdk/ndk/<version> or \$ANDROID_HOME/ndk/<version>). Skipping Android."
    return
  fi
  export ANDROID_NDK_HOME="$ndk_root"
  export ANDROID_NDK="$ndk_root"

  local host_tag
  host_tag="$(android_host_tag)"
  if [[ -z "$host_tag" ]]; then
    echo "Unsupported host for Android NDK build: $host_os/$host_arch"
    return
  fi

  local toolchain="$ndk_root/build/cmake/android.toolchain.cmake"
  if [[ ! -f "$toolchain" ]]; then
    echo "Android toolchain not found: $toolchain"
    return
  fi

  local abis=("armeabi-v7a" "arm64-v8a" "x86_64")
  for abi in "${abis[@]}"; do
    echo "Building Android ($abi)..."
    local build_dir="$BUILD_ROOT/android/$abi"
    cmake -S "$ROOT_DIR" -B "$build_dir" \
      -DDART=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DANDROID_ABI="$abi" \
      -DANDROID_PLATFORM=android-23
    cmake --build "$build_dir" --target colibri_dart --config Release

    local src_lib="$DART_NATIVE_DIR/libcolibri.so"
    if [[ ! -f "$src_lib" ]]; then
      echo "Missing Android output: $src_lib"
      exit 1
    fi
    local dest_dir="$DART_NATIVE_DIR/android/$abi"
    mkdir -p "$dest_dir"
    cp "$src_lib" "$dest_dir/libcolibri.so"
    echo "Copied: $dest_dir/libcolibri.so"

    if [[ -d "$FLUTTER_PLUGIN_DIR/android/src/main/jniLibs" ]]; then
      local plugin_dir="$FLUTTER_PLUGIN_DIR/android/src/main/jniLibs/$abi"
      mkdir -p "$plugin_dir"
      cp "$src_lib" "$plugin_dir/libcolibri.so"
      echo "Copied: $plugin_dir/libcolibri.so"
    fi
  done
}

build_ios() {
  if [[ "$host_os" != "Darwin" ]]; then
    echo "iOS build requires macOS. Skipping iOS."
    return
  fi

  echo "Building iOS XCFramework..."
  "$ROOT_DIR/bindings/swift/build_ios.sh"

  local src_xcframework="$ROOT_DIR/build/ios/ios_arm64/c4_swift.xcframework"
  if [[ ! -d "$src_xcframework" ]]; then
    echo "Missing iOS XCFramework: $src_xcframework"
    exit 1
  fi

  local dest_dir="$DART_NATIVE_DIR/ios"
  rm -rf "$dest_dir/c4_swift.xcframework"
  mkdir -p "$dest_dir"
  cp -R "$src_xcframework" "$dest_dir/"
  echo "Copied: $dest_dir/c4_swift.xcframework"

  if [[ -d "$FLUTTER_PLUGIN_DIR/ios/Frameworks" ]]; then
    local plugin_dir="$FLUTTER_PLUGIN_DIR/ios/Frameworks"
    rm -rf "$plugin_dir/c4_swift.xcframework"
    cp -R "$src_xcframework" "$plugin_dir/"
    echo "Copied: $plugin_dir/c4_swift.xcframework"
  fi
}

build_macos() {
  if [[ "$host_os" != "Darwin" ]]; then
    echo "macOS build requires a Mac host. Skipping macOS."
    return
  fi

  local macos_archs=("arm64" "x86_64")
  local dylibs=()
  for arch in "${macos_archs[@]}"; do
    echo "Building macOS ($arch)..."
    local build_dir="$BUILD_ROOT/macos_$arch"
    cmake -S "$ROOT_DIR" -B "$build_dir" \
      -DDART=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$arch"
    cmake --build "$build_dir" --target colibri_dart

    local src_lib="$DART_NATIVE_DIR/libcolibri.dylib"
    if [[ ! -f "$src_lib" ]]; then
      echo "Missing macOS output for $arch: $src_lib"
      exit 1
    fi
    local arch_lib="$BUILD_ROOT/macos/libcolibri_$arch.dylib"
    mkdir -p "$BUILD_ROOT/macos"
    cp "$src_lib" "$arch_lib"
    dylibs+=("$arch_lib")
  done

  local dest_dir="$DART_NATIVE_DIR/macos"
  mkdir -p "$dest_dir"
  local universal="$dest_dir/libcolibri.dylib"
  lipo -create "${dylibs[@]}" -output "$universal"
  echo "Copied: $universal (universal)"

  local plugin_frameworks="$FLUTTER_PLUGIN_DIR/macos/Frameworks"
  if [[ -d "$FLUTTER_PLUGIN_DIR/macos" ]]; then
    mkdir -p "$plugin_frameworks"
    cp "$universal" "$plugin_frameworks/libcolibri.dylib"
    echo "Copied: $plugin_frameworks/libcolibri.dylib"
  fi
}

build_linux() {
  if [[ "$host_os" != "Linux" ]]; then
    echo "Linux build requires a Linux host. Skipping Linux."
    return
  fi

  echo "Building Linux (x86_64)..."
  local build_dir="$BUILD_ROOT/linux"
  cmake -S "$ROOT_DIR" -B "$build_dir" \
    -DDART=ON \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$build_dir" --target colibri_dart

  local src_lib="$build_dir/bindings/dart/libcolibri.so"
  if [[ ! -f "$src_lib" ]]; then
    src_lib="$DART_NATIVE_DIR/libcolibri.so"
  fi
  if [[ ! -f "$src_lib" ]]; then
    echo "Missing Linux output: $src_lib"
    exit 1
  fi
  local dest_dir="$DART_NATIVE_DIR/linux"
  mkdir -p "$dest_dir"
  cp "$src_lib" "$dest_dir/libcolibri.so"
  echo "Copied: $dest_dir/libcolibri.so"

  local plugin_lib="$FLUTTER_PLUGIN_DIR/linux/lib"
  if [[ -d "$FLUTTER_PLUGIN_DIR/linux" ]]; then
    mkdir -p "$plugin_lib"
    cp "$src_lib" "$plugin_lib/libcolibri.so"
    echo "Copied: $plugin_lib/libcolibri.so"
  fi
}

build_windows() {
  if [[ "$host_os" != "MINGW"* && "$host_os" != "MSYS"* && "$host_os" != "CYGWIN"* && "$host_os" != "Windows_NT" ]]; then
    echo "Windows build requires a Windows host. Skipping Windows."
    return
  fi

  echo "Building Windows DLL..."
  local build_dir="$BUILD_ROOT/windows"
  cmake -S "$ROOT_DIR" -B "$build_dir" -DDART=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build "$build_dir" --config Release --target colibri_dart

  local src_lib="$DART_NATIVE_DIR/colibri.dll"
  if [[ ! -f "$src_lib" ]]; then
    echo "Missing Windows output: $src_lib"
    exit 1
  fi
  local dest_dir="$DART_NATIVE_DIR/windows"
  mkdir -p "$dest_dir"
  cp "$src_lib" "$dest_dir/colibri.dll"
  echo "Copied: $dest_dir/colibri.dll"
}

if $want_android; then
  build_android
fi
if $want_ios; then
  build_ios
fi
if $want_macos; then
  build_macos
fi
if $want_linux; then
  build_linux
fi
if $want_windows; then
  build_windows
fi

echo "Done."
