#!/usr/bin/env python3
"""
Compare C (ctest) and Dart test results.

Usage:
  C4_BUILD_DIR=/path/to/build python3 scripts/compare_c_dart_tests.py

Optional env:
  DART_BINDINGS_DIR (default: bindings/dart)
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path


def run_cmd(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)


def parse_ctest_summary(output: str) -> tuple[int, int]:
    """
    Return (total, failed) counts from ctest output.
    """
    # Typical format: "100% tests passed, 0 tests failed out of N"
    match = re.search(r"tests passed,\s+(\d+)\s+tests failed out of\s+(\d+)", output)
    if match:
        failed = int(match.group(1))
        total = int(match.group(2))
        return total, failed
    # Fallback: try to count "Test #"
    total = len(re.findall(r"Test #\d+", output))
    failed = len(re.findall(r"\*\*\*Failed", output))
    return total, failed


def parse_dart_json(output: str) -> tuple[int, int]:
    """
    Return (total, failed) counts from `dart test --reporter json`.
    """
    total = 0
    failed = 0
    for line in output.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("type") == "testDone":
            total += 1
            if event.get("result") != "success":
                failed += 1
    return total, failed


def parse_compare_results(output: str) -> tuple[int, int, int, list[dict]]:
    total = 0
    failed = 0
    skipped = 0
    failures: list[dict] = []
    for line in output.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("skipped"):
            skipped += 1
            continue
        if "passed" in event:
            total += 1
            if not event.get("passed"):
                failed += 1
                failures.append(event)
    return total, failed, skipped, failures


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = os.getenv("C4_BUILD_DIR", "./build")

    dart_dir = Path(os.getenv("DART_BINDINGS_DIR", "bindings/dart"))
    dart_dir = (repo_root / dart_dir).resolve()
    build_dir_path = Path(build_dir).resolve()

    if not build_dir_path.exists():
        print(f"C4_BUILD_DIR does not exist: {build_dir_path}")
        return 2

    if not dart_dir.exists():
        print(f"Dart bindings dir does not exist: {dart_dir}")
        return 2

    print("Running C tests (ctest)...")
    ctest = run_cmd(["ctest", "--output-on-failure", "--no-tests=error"], cwd=build_dir_path)
    ctest_out = (ctest.stdout or "") + (ctest.stderr or "")
    c_total, c_failed = parse_ctest_summary(ctest_out)
    c_status = "PASS" if ctest.returncode == 0 else "FAIL"

    print("Running Dart tests...")
    dart = run_cmd(["dart", "test", "--reporter", "json"], cwd=dart_dir)
    dart_out = (dart.stdout or "") + (dart.stderr or "")
    d_total, d_failed = parse_dart_json(dart_out)
    d_status = "PASS" if dart.returncode == 0 else "FAIL"

    print("Comparing Dart results against C fixtures...")
    compare = run_cmd(["dart", "run", "tool/compare_results.dart"], cwd=dart_dir)
    compare_out = (compare.stdout or "") + (compare.stderr or "")
    r_total, r_failed, r_skipped, r_failures = parse_compare_results(compare_out)
    r_status = "PASS" if compare.returncode == 0 and r_failed == 0 else "FAIL"

    print("")
    print("=== Test Summary ===")
    print(f"C tests:    {c_status} ({c_total} total, {c_failed} failed)")
    print(f"Dart tests: {d_status} ({d_total} total, {d_failed} failed)")
    print("====================")
    print(f"Result compare: {r_status} ({r_total} total, {r_failed} mismatched, {r_skipped} skipped)")

    # Compare results: both must pass to be considered matching
    if c_failed == 0 and d_failed == 0 and r_failed == 0 and ctest.returncode == 0 and dart.returncode == 0:
        print("✅ C and Dart tests both pass, results match fixtures.")
        return 0

    if (ctest.returncode == 0) != (dart.returncode == 0):
        if "Operation not permitted" in dart_out or "Permission denied" in dart_out:
            print("⚠️ Dart tests failed due to a permissions error.")
            print("   If you are running in a sandbox, re-run with full permissions.")
        print("❌ Mismatch: one suite failed while the other passed.")
        return 1

    if r_failed:
        print("❌ Dart results do not match C fixtures.")
        for failure in r_failures[:5]:
            print(f"   - {failure.get('name')}: expected != actual")
        if len(r_failures) > 5:
            print(f"   ... and {len(r_failures) - 5} more")

    print("❌ Both suites failed or have failing tests.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
