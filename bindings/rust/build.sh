#!/usr/bin/env bash
set -euo pipefail

# Simple multi-target build helper for Colibri Rust bindings

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUST_DIR="$ROOT_DIR/bindings/rust"
BUILD_DIR="$ROOT_DIR/build"

usage() {
  echo "Usage: $0 [--release] [--clean] [--cmake-flags '...'] [--targets 'list']" 
  echo "\nExamples:" 
  echo "  $0 --release" 
  echo "  $0 --clean --cmake-flags '-DSHAREDLIB=1'" 
  echo "  $0 --targets 'x86_64-apple-darwin aarch64-apple-darwin'"
}

RELEASE=0
DEBUG=0
CLEAN=0
CMAKE_FLAGS=""
TARGETS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --release) RELEASE=1; shift ;;
    --debug) DEBUG=1; shift ;;
    --clean) CLEAN=1; shift ;;
    --cmake-flags) CMAKE_FLAGS="$2"; shift 2 ;;
    --targets) TARGETS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1"; usage; exit 1 ;;
  esac
done

if [[ $CLEAN -eq 1 ]]; then
  echo "[CLEAN] $BUILD_DIR"
  rm -rf "$BUILD_DIR"
  rm -rf "$RUST_DIR/target"
fi

# Determine build type
BUILD_TYPE="Release"
if [[ $DEBUG -eq 1 ]]; then
  BUILD_TYPE="Debug"
elif [[ $RELEASE -eq 1 ]]; then
  BUILD_TYPE="Release"
fi

echo "[INFO] Build type: $BUILD_TYPE"

# Build native C lib once via CMake unless user overrides
if [[ -z "${COLIBRI_LIB_DIR:-}" ]]; then
  echo "[CMAKE] Configure & Build C library"
  mkdir -p "$BUILD_DIR/default"
  pushd "$BUILD_DIR/default" >/dev/null
  cmake -DSHAREDLIB=1 -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS ../.. 
  make -j$(sysctl -n hw.ncpu)
  popd >/dev/null
  export COLIBRI_LIB_DIR="$BUILD_DIR/default/lib"
  echo "[INFO] COLIBRI_LIB_DIR=$COLIBRI_LIB_DIR"
fi

pushd "$RUST_DIR" >/dev/null

if [[ -z "$TARGETS" ]]; then
  # Default: host build only
  if [[ $DEBUG -eq 1 ]]; then
    cargo build
  elif [[ $RELEASE -eq 1 ]]; then
    cargo build --release
  else
    cargo build
  fi
else
  for t in $TARGETS; do
    echo "[CARGO] Build target $t"
    if [[ $DEBUG -eq 1 ]]; then
      cargo build --target "$t"
    elif [[ $RELEASE -eq 1 ]]; then
      cargo build --target "$t" --release
    else
      cargo build --target "$t"
    fi
  done
fi

popd >/dev/null

echo "[DONE] Rust bindings built."


