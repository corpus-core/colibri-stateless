#!/usr/bin/env bash
set -euo pipefail

python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/compare_c_dart_tests.py"
