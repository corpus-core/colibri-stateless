#!/bin/bash

# Script to create test data for RPC tests
# Usage: ./create_test.sh <testname> <rpc_method> <rpc_args...>
#
# Example:
#   ./create_test.sh eth_getBlockByNumber1 eth_getBlockByNumber latest false
#   ./create_test.sh eth_call1 eth_call '{"to":"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48","data":"0x70a08231..."}' latest
#
# This script:
# 1. Creates a proof using colibri-prover
# 2. Verifies the proof using colibri-verifier
# 3. Copies state files to test/data/<testname>/
# 4. Generates test.json with the RPC request and expected result

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check arguments
if [ "$#" -lt 3 ]; then
    echo -e "${RED}Error: Not enough arguments${NC}"
    echo "Usage: $0 <testname> <rpc_method> <rpc_args...>"
    echo ""
    echo "Examples:"
    echo "  $0 eth_getBlockByNumber1 eth_getBlockByNumber latest false"
    echo "  $0 eth_call1 eth_call '{\"to\":\"0xA0b...\",\"data\":\"0x70a08231...\"}' latest"
    exit 1
fi

TESTNAME="$1"
RPC_METHOD="$2"
shift 2
RPC_ARGS="$@"

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/default"
TEST_DATA_DIR="$PROJECT_ROOT/test/data/$TESTNAME"
PROOF_FILE="$SCRIPT_DIR/proof.ssz"
RESULT_FILE="$SCRIPT_DIR/result.txt"

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}Creating test: $TESTNAME${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}Method:${NC} $RPC_METHOD"
echo -e "${BLUE}Args:${NC}   $RPC_ARGS"
echo ""

# Set up temporary state directory for this test run
TEMP_STATE_DIR="$SCRIPT_DIR/.test_states_temp"
rm -rf "$TEMP_STATE_DIR"
mkdir -p "$TEMP_STATE_DIR"
export C4_STATES_DIR="$TEMP_STATE_DIR"

# Create proof
echo -e "${BLUE}📝 Creating proof...${NC}"
"$BUILD_DIR/bin/colibri-prover" -o "$PROOF_FILE" -t "$TESTNAME" "$RPC_METHOD" $RPC_ARGS

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Proof generation failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Proof created${NC}"

# Create test data directory first (needed for -t flag)
echo -e "${BLUE}📁 Creating test directory...${NC}"
mkdir -p "$TEST_DATA_DIR"

# Verify proof and let verifier automatically create test.json
# Note: -t expects testname, not full path. Verifier writes to test/data/<testname>/test.json
echo -e "${BLUE}🔍 Verifying proof and generating test.json...${NC}"
"$BUILD_DIR/bin/colibri-verifier" -i "$PROOF_FILE" -t "$TESTNAME" "$RPC_METHOD" $RPC_ARGS | tee "$RESULT_FILE"

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Verification failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Proof verified and test.json created${NC}"

# Copy state files from temporary directory
echo -e "${BLUE}💾 Copying state files...${NC}"
if [ -d "$TEMP_STATE_DIR" ]; then
    # Copy all state files (states_*, sync_*, code_*)
    cp -v "$TEMP_STATE_DIR"/states_* "$TEST_DATA_DIR/" 2>/dev/null || true
    cp -v "$TEMP_STATE_DIR"/sync_* "$TEST_DATA_DIR/" 2>/dev/null || true
    cp -v "$TEMP_STATE_DIR"/code_* "$TEST_DATA_DIR/" 2>/dev/null || true
    
    STATE_FILE_COUNT=$(ls "$TEST_DATA_DIR"/states_* "$TEST_DATA_DIR"/sync_* 2>/dev/null | wc -l)
    echo -e "${GREEN}✓ Copied $STATE_FILE_COUNT state files${NC}"
else
    echo -e "${YELLOW}⚠️  No state directory found${NC}"
fi

# Copy proof to test directory (optional - tests build their own proofs)
# This is mainly useful for manual inspection and comparison
echo -e "${BLUE}📄 Copying proof for reference...${NC}"
cp "$PROOF_FILE" "$TEST_DATA_DIR/proof.ssz"
echo -e "${GREEN}✓ Reference proof saved to $TEST_DATA_DIR/proof.ssz${NC}"

# test.json was already created by verifier with -t flag
echo -e "${BLUE}📝 Generated test.json:${NC}"
cat "$TEST_DATA_DIR/test.json"

# Cleanup
rm -f "$PROOF_FILE" "$RESULT_FILE"
rm -rf "$TEMP_STATE_DIR"

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}✅ Test data created successfully!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}Location:${NC} $TEST_DATA_DIR"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "1. Review the generated test.json and mock files"
echo "2. Create test file in test/unittests/test_${TESTNAME}.c"
echo "3. Add test to test/unittests/CMakeLists.txt"
echo "4. Run: ./scripts/run_coverage.sh"
