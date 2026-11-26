#!/usr/bin/env bash
# Classify the latest scan-build HTML reports.
# Prints one line per report: "scan-build REAL" or "scan-build FALSE POSITIVE",
# then "scan-build verdict: N real, M false positive".
# Exit 0 only when every report is a listed false positive. A real finding exits 1.
#
# Usage: scan-build-apply-suppressions.sh <results_dir> <suppressions_file>
#   results_dir       e.g. scan-build-results (or build/scan-build-results)
#   suppressions_file e.g. scripts/scan-build-suppressions.txt
#
# Call from the directory that contains (or is) results_dir.

# Do not use set -e: conditions like [[ ... ]] and [ ... ] are used for flow control and must not exit
RESULTS_DIR="${1:?usage: scan-build-apply-suppressions.sh <results_dir> <suppressions_file>}"
SUPPRESSIONS_FILE="${2:?usage: scan-build-apply-suppressions.sh <results_dir> <suppressions_file>}"

LATEST_REPORT_DIR=$(find "$RESULTS_DIR" -type d -name "20*" 2>/dev/null | sort | tail -n 1)

if [ -z "$LATEST_REPORT_DIR" ] || [ ! -d "$LATEST_REPORT_DIR" ] || [ ! -f "$SUPPRESSIONS_FILE" ]; then
  exit 1
fi

UNSURPRESSED=0
SUPPRESSED_COUNT=0
REAL_COUNT=0
for report in "$LATEST_REPORT_DIR"/report-*.html; do
  [ -f "$report" ] || continue
  BUGFILE="$(sed -n 's/.*<!-- BUGFILE \(.*\) -->.*/\1/p' "$report" | head -1)"
  BUGFUNC="$(sed -n 's/.*<!-- FUNCTIONNAME \(.*\) -->.*/\1/p' "$report" | head -1)"
  BUGDESC="$(sed -n 's/.*<!-- BUGDESC \(.*\) -->.*/\1/p' "$report" | head -1)"
  BUGLINE="$(sed -n 's/.*<!-- BUGLINE \(.*\) -->.*/\1/p' "$report" | head -1)"
  SUPPRESSED=0
  while IFS= read -r line || [ -n "$line" ]; do
    if [[ "$line" =~ ^#.*$ ]] || [[ -z "${line// }" ]]; then continue; fi
    IFS=: read -r file_pat func_pat desc_pat <<< "$line"
    if [ -z "$file_pat" ]; then continue; fi
    if [[ "$BUGFILE" == *"$file_pat"* ]] && [[ "$BUGFUNC" == "$func_pat" ]] && [[ "$BUGDESC" == *"$desc_pat"* ]]; then
      SUPPRESSED=1
      break
    fi
  done < "$SUPPRESSIONS_FILE" || true
  shortfile="${BUGFILE#*colibri-stateless/}"
  if [ "$SUPPRESSED" -eq 0 ]; then
    UNSURPRESSED=1
    REAL_COUNT=$((REAL_COUNT + 1))
    echo "scan-build REAL: ${shortfile}:${BUGLINE}: ${BUGFUNC}: ${BUGDESC}"
  else
    SUPPRESSED_COUNT=$((SUPPRESSED_COUNT + 1))
    echo "scan-build FALSE POSITIVE: ${shortfile}:${BUGLINE}: ${BUGFUNC}: ${BUGDESC}"
  fi
done
echo "scan-build verdict: ${REAL_COUNT} real, ${SUPPRESSED_COUNT} false positive"

REPORT_COUNT=$(find "$LATEST_REPORT_DIR" -maxdepth 1 -name 'report-*.html' 2>/dev/null | wc -l | tr -d ' ')
if [ "$UNSURPRESSED" -eq 0 ] && [ "${REPORT_COUNT}" -gt 0 ]; then
  exit 0
fi
exit 1
