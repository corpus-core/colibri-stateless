#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${C4_BUILD_DIR:-}" ]]; then
  echo "C4_BUILD_DIR is required (path to CMake build directory)."
  exit 2
fi

python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/compare_c_dart_tests.py"
