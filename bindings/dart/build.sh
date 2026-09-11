#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-dart"

echo "Repository root: ${ROOT_DIR}"
echo "Build directory: ${BUILD_DIR} (not bindings/dart/build-dart)"
echo ""

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DDART=ON -DCHAIN_OP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target colibri_dart --config Release
