#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_DIR="test/coverage"
mkdir -p "$OUT_DIR"

dart test --coverage="$OUT_DIR"
dart run coverage:format_coverage \
  --lcov \
  --in="$OUT_DIR" \
  --out="$OUT_DIR/lcov.info" \
  --report-on="lib"
