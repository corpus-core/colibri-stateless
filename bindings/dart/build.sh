#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-dart"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DDART=ON -DCHAIN_OP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target colibri_dart --config Release
