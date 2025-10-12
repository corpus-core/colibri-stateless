#!/bin/bash
# Valgrind Server Wrapper Script
# Runs the C4 server under valgrind with production-like configuration

set -e

echo "============================================"
echo "C4 Server - Valgrind Memory Debugging Mode"
echo "============================================"
echo ""
echo "Configuration:"
echo "  VALGRIND_OPTS: ${VALGRIND_OPTS}"
echo "  VALGRIND_LOG: ${VALGRIND_LOG}"
echo "  Suppressions: /app/server.supp"
echo ""

# Default valgrind options if not set
: ${VALGRIND_OPTS:="--leak-check=full --show-leak-kinds=definite --track-origins=yes --fair-sched=yes"}
: ${VALGRIND_LOG:="/app/logs/valgrind.log"}

# Build valgrind command
VALGRIND_CMD="valgrind \
  ${VALGRIND_OPTS} \
  --suppressions=/app/server.supp \
  --log-file=${VALGRIND_LOG} \
  --error-exitcode=0"

# If VALGRIND_VERBOSE is set, add verbose output
if [ -n "$VALGRIND_VERBOSE" ]; then
  VALGRIND_CMD="$VALGRIND_CMD -v"
fi

# If VALGRIND_XML is set, also output XML
if [ -n "$VALGRIND_XML" ]; then
  VALGRIND_CMD="$VALGRIND_CMD --xml=yes --xml-file=${VALGRIND_LOG}.xml"
fi

echo "Starting server under valgrind..."
echo "Command: $VALGRIND_CMD /app/server $@"
echo ""
echo "Logs will be written to: ${VALGRIND_LOG}"
echo "Server output below:"
echo "============================================"
echo ""

# Trap signals to ensure valgrind cleanup
trap 'echo ""; echo "Shutting down valgrind..."; kill -TERM $PID 2>/dev/null; wait $PID; echo "Valgrind report saved to ${VALGRIND_LOG}"; exit 0' SIGTERM SIGINT

# Run valgrind with server
$VALGRIND_CMD /app/server "$@" &
PID=$!

# Wait for valgrind to finish
wait $PID
EXIT_CODE=$?

echo ""
echo "============================================"
echo "Server stopped with exit code: $EXIT_CODE"
echo ""
echo "Valgrind Memory Analysis Summary:"
echo "--------------------------------------------"

# Show summary of valgrind results
if [ -f "${VALGRIND_LOG}" ]; then
  # Extract key information
  echo ""
  grep -A 10 "LEAK SUMMARY" "${VALGRIND_LOG}" || echo "No leak summary found (yet)"
  echo ""
  echo "Definite leaks:"
  grep "definitely lost:" "${VALGRIND_LOG}" || echo "  0 bytes (none detected)"
  echo ""
  echo "Full report: ${VALGRIND_LOG}"
  echo ""
  
  # Check for critical errors
  DEFINITE_LOST=$(grep "definitely lost:" "${VALGRIND_LOG}" | grep -oE "[0-9,]+ bytes" | head -1 | tr -d ',')
  if [ -n "$DEFINITE_LOST" ] && [ "$DEFINITE_LOST" != "0 bytes" ]; then
    echo "⚠️  WARNING: Definite memory leaks detected: $DEFINITE_LOST"
    echo "   Review the full report for details."
  else
    echo "✅ No definite leaks detected"
  fi
else
  echo "⚠️  Valgrind log not found at ${VALGRIND_LOG}"
fi

echo "============================================"
exit $EXIT_CODE

