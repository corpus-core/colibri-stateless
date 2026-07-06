#!/usr/bin/env bash
# Resolve macOS SDK for CMake (avoids stale CMAKE_OSX_SYSROOT after Xcode updates).
dart_cmake_osx_args() {
  DART_CMAKE_OSX_ARGS=()
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return 0
  fi
  local sdk
  sdk="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
  if [[ -n "$sdk" && -d "$sdk" ]]; then
    DART_CMAKE_OSX_ARGS=(-DCMAKE_OSX_SYSROOT="$sdk")
  fi
}
