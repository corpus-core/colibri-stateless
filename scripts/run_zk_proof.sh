#!/bin/bash
set -e

# Defaults
PERIOD=""
MODE="--execute"
OUTPUT_DIR=".zk_proofs"
REMOTE_URL="https://mainnet1.colibri-proof.tech/"
GROTH16=""

# Parse Args
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --prove) MODE="--prove" ;;
        --execute) MODE="--execute" ;;
        --groth16) GROTH16="--groth16" ;;
        --period) PERIOD="$2"; shift ;;
        --output) OUTPUT_DIR="$2"; shift ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

if [ -z "$PERIOD" ]; then
    echo "Usage: ./run_zk_proof.sh --period <period> [--prove] [--groth16] [--output <dir>]"
    exit 1
fi

# Ensure output dir exists relative to workspace root
mkdir -p "$OUTPUT_DIR"
# Convert to absolute path for safety when changing dirs
OUTPUT_DIR_ABS=$(cd "$OUTPUT_DIR" && pwd)
INPUT_FILE="$OUTPUT_DIR_ABS/sync_${PERIOD}.ssz"

if [ -n "$GROTH16" ]; then
    PROOF_FILE="$OUTPUT_DIR_ABS/proof_${PERIOD}_groth16.bin"
    PROOF_RAW_FILE="$OUTPUT_DIR_ABS/proof_${PERIOD}_raw.bin"
    VK_FILE="$OUTPUT_DIR_ABS/vk_${PERIOD}_groth16.bin"
    PUBLIC_VALUES_FILE="$OUTPUT_DIR_ABS/public_values_${PERIOD}.bin"
else
    PROOF_FILE="$OUTPUT_DIR_ABS/proof_${PERIOD}.bin"
    VK_FILE="$OUTPUT_DIR_ABS/vk_${PERIOD}.bin"
    PUBLIC_VALUES_FILE="$OUTPUT_DIR_ABS/public_values_${PERIOD}.bin"
fi

# Fetch Input Data if missing
if [ ! -f "$INPUT_FILE" ]; then
    echo "📥 Fetching sync data for period $PERIOD..."
    curl -X POST "$REMOTE_URL" \
         -H "content-type: application/json" \
         -d "{\"method\":\"eth_proof_sync\",\"params\":[$PERIOD],\"id\":1,\"jsonrpc\":\"2.0\"}" \
         --output "$INPUT_FILE" --fail --silent
    
    # Check if file is valid (not empty)
    if [ ! -s "$INPUT_FILE" ]; then
        echo "❌ Failed to fetch data or empty response"
        rm -f "$INPUT_FILE"
        exit 1
    fi
    echo "✅ Data saved to $INPUT_FILE"
else
    echo "📂 Using existing data: $INPUT_FILE"
fi

if [ "$MODE" == "--prove" ]; then
    echo "🚀 Running in PROVE mode (this will take time!)"
    if [ -n "$GROTH16" ]; then
        echo "📦 Groth16 mode enabled (requires Docker)"
    fi
else
    echo "⚡ Running in EXECUTE mode (fast simulation)"
fi

# Setup Env
export PATH=$HOME/.cargo/bin:$HOME/.sp1/bin:$PATH

# Workspace Root relative to this script (scripts/ -> ./)
WORKSPACE_ROOT=$(cd "$(dirname "$0")/.." && pwd)

# Build Guest
echo "🔨 Building Guest Program..."
(
    export RUSTC=$HOME/.sp1/toolchains/PkFc33VNGO/bin/rustc
    export RUSTFLAGS='--cfg getrandom_backend="custom" -C link-arg=-Ttext=0x00201000 -C link-arg=--image-base=0x00200800 -C panic=abort'
    cd "$WORKSPACE_ROOT/src/chains/eth/zk_proof/program"
    cargo build --release --target riscv32im-succinct-zkvm-elf
)

# Find ELF
ELF=$(find "$WORKSPACE_ROOT/src/chains/eth/zk_proof/target/riscv32im-succinct-zkvm-elf/release/deps" -name "eth_sync_program*" -type f -not -name "*.*" | head -n 1)

if [ -z "$ELF" ]; then
    # Fallback search
    ELF=$(find "$WORKSPACE_ROOT/src/chains/eth/zk_proof/program/target/riscv32im-succinct-zkvm-elf/release/deps" -name "eth_sync_program*" -type f -not -name "*.*" 2>/dev/null | head -n 1)
fi

if [ -z "$ELF" ]; then
    echo "❌ Error: Could not find guest ELF binary."
    exit 1
fi
echo "✅ Found ELF: $ELF"

# Build Host
echo "🔨 Building Host Script..."
unset RUSTFLAGS
unset RUSTC
cd "$WORKSPACE_ROOT/src/chains/eth/zk_proof/script"
cargo build --release

# Run Host
echo "🏃 Running Host Script..."
HOST_BINARY="$WORKSPACE_ROOT/src/chains/eth/zk_proof/target/release/eth-sync-script"

if [ ! -f "$HOST_BINARY" ]; then
    echo "❌ Error: Host binary not found at $HOST_BINARY"
    exit 1
fi

# Pass Output Path via Env Var
export PROOF_OUTPUT_FILE="$PROOF_FILE"
export VK_OUTPUT_FILE="$VK_FILE"
export PUBLIC_VALUES_FILE="$PUBLIC_VALUES_FILE"
if [ -n "$PROOF_RAW_FILE" ]; then
    export PROOF_RAW_FILE="$PROOF_RAW_FILE"
fi

# Run
ELF_PATH="$ELF" "$HOST_BINARY" $MODE $GROTH16 --input-file "$INPUT_FILE"

if [ "$MODE" == "--prove" ]; then
    if [ -f "$PROOF_FILE" ]; then
        echo "🎉 Proof saved to: $PROOF_FILE"
    fi
    if [ -n "$PROOF_RAW_FILE" ] && [ -f "$PROOF_RAW_FILE" ]; then
        echo "🎉 Raw Proof saved to: $PROOF_RAW_FILE"
    fi
fi
if [ -f "$PUBLIC_VALUES_FILE" ]; then
    echo "🎉 Public Values saved to: $PUBLIC_VALUES_FILE"
fi
