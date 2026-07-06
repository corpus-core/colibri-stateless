#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-dart"

# shellcheck source=scripts/cmake_osx_args.sh
source "${SCRIPT_DIR}/scripts/cmake_osx_args.sh"
dart_cmake_osx_args

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DDART=ON -DCMAKE_BUILD_TYPE=Release \
  "${DART_CMAKE_OSX_ARGS[@]}"
cmake --build "${BUILD_DIR}" --target colibri_dart --config Release
