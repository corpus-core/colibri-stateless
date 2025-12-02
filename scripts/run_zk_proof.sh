#!/bin/bash
set -e

# Defaults
PERIOD=""
PREV_PERIOD=""
START_PERIOD=""
END_PERIOD=""
MODE="--execute"
OUTPUT_DIR="build/default/.period_store"
REMOTE_URL="https://mainnet1.colibri-proof.tech/"
GROTH16=""
USE_NETWORK=false
PRIVATE_KEY_ARG=""

# Parse Args
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --prove) MODE="--prove" ;;
        --execute) MODE="--execute" ;;
        --groth16) GROTH16="--groth16" ;;
        --network) USE_NETWORK=true ;;
        --private-key) PRIVATE_KEY_ARG="$2"; shift ;;
        --period) PERIOD="$2"; shift ;;
        --prev-period) PREV_PERIOD="$2"; shift ;;
        --start-period) START_PERIOD="$2"; shift ;;
        --end-period) END_PERIOD="$2"; shift ;;
        --output) OUTPUT_DIR="$2"; shift ;;
        --rpc) REMOTE_URL="$2"; shift ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

# --- SP1 NETWORK SETUP ---
if [ "$USE_NETWORK" = true ]; then
    export SP1_PROVER="network"
    if [ -n "$PRIVATE_KEY_ARG" ]; then
        export SP1_PRIVATE_KEY="$PRIVATE_KEY_ARG"
    fi

    if [ -z "$SP1_PRIVATE_KEY" ]; then
        echo "❌ Error: Network mode selected but SP1_PRIVATE_KEY not found."
        echo "   Please provide it via --private-key <key> or export SP1_PRIVATE_KEY=<key>"
        exit 1
    fi
    echo "🌐 SP1 Network Mode: ENABLED"
fi

# --- SP1 TOOLCHAIN SETUP ---

# Check for sp1up
if ! command -v sp1up &> /dev/null; then
    echo "⚠️  sp1up not found. Installing SP1 toolchain..."
    curl -L https://sp1.succinct.xyz | bash
    source $HOME/.bashrc || source $HOME/.zshrc || true
fi

# Ensure cargo-prove is installed
if ! command -v cargo-prove &> /dev/null; then
     echo "⚠️  cargo-prove not found. Installing..."
     sp1up
fi

# Check Rust version (Host Compiler only)
# This affects the host script, not the guest program (VK).
REQUIRED_RUST_HOST="1.81.0"
CURRENT_RUST=$(rustc --version | cut -d ' ' -f 2)

# Add SP1 bin to PATH for this session
export PATH=$HOME/.sp1/bin:$PATH

# Locate SP1 Toolchain
# CRITICAL: We pin a specific toolchain version to ensure the Verification Key (VK)
# remains stable across different machines/developers.
# PkFc33VNGO corresponds to sp1 v4.0.0 / v5.0.0 specific toolchain
PINNED_TOOLCHAIN="PkFc33VNGO"
SP1_TOOLCHAIN_DIR="$HOME/.sp1/toolchains"
RUSTC_PATH="$SP1_TOOLCHAIN_DIR/$PINNED_TOOLCHAIN/bin/rustc"

if [ ! -f "$RUSTC_PATH" ]; then
    echo "⚠️  Pinned SP1 toolchain ($PINNED_TOOLCHAIN) not found."
    echo "   Attempting to install/use specific version..."
    # sp1up doesn't easily support installing a specific hash directly via CLI in all versions,
    # but we can warn the user or try to find a compatible one.
    # For now, we will fallback to the latest but WARN heavily.
    
    LATEST_TOOLCHAIN=$(ls -t "$SP1_TOOLCHAIN_DIR" 2>/dev/null | head -n 1)
    if [ -n "$LATEST_TOOLCHAIN" ]; then
        echo "⚠️  WARNING: Using latest toolchain ($LATEST_TOOLCHAIN) instead of pinned ($PINNED_TOOLCHAIN)."
        echo "   This MAY change the Verification Key/Program Hash!"
        RUSTC_PATH="$SP1_TOOLCHAIN_DIR/$LATEST_TOOLCHAIN/bin/rustc"
    else
         echo "❌ Error: No SP1 toolchain found. Please run 'sp1up'."
         exit 1
    fi
else
    echo "✅ Using Pinned SP1 Toolchain: $PINNED_TOOLCHAIN"
fi

export RUSTC="$RUSTC_PATH"

# LOOP MODE
if [ -n "$START_PERIOD" ] && [ -n "$END_PERIOD" ]; then
    echo "🔄 Running in Loop Mode: $START_PERIOD to $END_PERIOD"
    
    SCRIPT_PATH="$(cd "$(dirname "$0")" && pwd)/run_zk_proof.sh"
    
    NETWORK_OPT=""
    if [ "$USE_NETWORK" = true ]; then NETWORK_OPT="--network"; fi

    # Step 1: Initial Proof (Start Period)
    echo "🏁 Step 1: Initial Proof for Period $START_PERIOD"
    START_TIME=$(date +%s)
    "$SCRIPT_PATH" --period "$START_PERIOD" $MODE $GROTH16 $NETWORK_OPT --output "$OUTPUT_DIR"
    END_TIME=$(date +%s)
    echo "⏱️  Initial Proof for Period $START_PERIOD took $((END_TIME - START_TIME)) seconds"
    
    # Loop
    PREV=$START_PERIOD
    for (( i=START_PERIOD+1; i<=END_PERIOD; i++ )); do
        echo "🔗 Step $((i-START_PERIOD+1)): Recursive Proof for Period $i (prev: $PREV)"
        START_TIME=$(date +%s)
        "$SCRIPT_PATH" --period "$i" --prev-period "$PREV" $MODE $GROTH16 $NETWORK_OPT --output "$OUTPUT_DIR"
        END_TIME=$(date +%s)
        echo "⏱️  Proof for Period $i took $((END_TIME - START_TIME)) seconds"
        PREV=$i
    done
    
    echo "✅ Loop completed successfully from $START_PERIOD to $END_PERIOD"
    exit 0
fi

if [ -z "$PERIOD" ]; then
    echo "Usage: ./run_zk_proof.sh --period <period> [--prev-period <period>] [--prove] [--groth16] [--output <dir>]"
    echo "   OR: ./run_zk_proof.sh --start-period <start> --end-period <end> [--prove] [--groth16]"
    exit 1
fi

# Convert to absolute path for safety when changing dirs
mkdir -p "$OUTPUT_DIR/${PERIOD}"
OUTPUT_DIR_ABS=$(cd "$OUTPUT_DIR/${PERIOD}" && pwd)
INPUT_FILE="$OUTPUT_DIR_ABS/sync.ssz"


if [ -n "$GROTH16" ]; then
    PROOF_FILE="$OUTPUT_DIR_ABS/zk_groth16.bin"
    PROOF_RAW_FILE="$OUTPUT_DIR_ABS/zk_proof_g16.bin"
    VK_FILE="$OUTPUT_DIR_ABS/zk_vk.bin"
    PUBLIC_VALUES_FILE="$OUTPUT_DIR_ABS/zk_pub.bin"
else
    PROOF_FILE="$OUTPUT_DIR_ABS/zk_proof.bin"
    VK_FILE="$OUTPUT_DIR_ABS/vk_raw.bin"
    PUBLIC_VALUES_FILE="$OUTPUT_DIR_ABS/zk_pub.bin"
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

# Prepare Recursion Args
PREV_PROOF_ARGS=""

# Auto-detect previous period if not explicitly set
IS_AUTO_PREV=false
if [ -z "$PREV_PERIOD" ] && [ -n "$PERIOD" ]; then
    PREV_PERIOD=$((PERIOD - 1))
    IS_AUTO_PREV=true
fi

if [ -n "$PREV_PERIOD" ]; then
    # For recursion, we always need the COMPRESSED proof of the previous period.
    # If we ran with --groth16, the script now saves both _groth16.bin and .bin (compressed).
    # We look for the .bin file.
    PREV_PROOF_FILE="$OUTPUT_DIR_ABS/../${PREV_PERIOD}/zk_proof.bin"
    PREV_VK_FILE="$OUTPUT_DIR_ABS/../${PREV_PERIOD}/zk_vk_raw.bin"
    
    MISSING_FILES=false
    if [ ! -f "$PREV_PROOF_FILE" ]; then MISSING_FILES=true; fi
    if [ ! -f "$PREV_VK_FILE" ]; then MISSING_FILES=true; fi

    if [ "$MISSING_FILES" = true ]; then
        if [ "$IS_AUTO_PREV" = true ]; then
            echo "ℹ️  No previous proof found for period $PREV_PERIOD. Starting fresh (no recursion)."
        else
            echo "❌ Error: Previous compressed proof not found at $PREV_PROOF_FILE"
            echo "   Please run period $PREV_PERIOD first."
            exit 1
        fi
    else
        echo "🔗 Chaining with previous period $PREV_PERIOD"
        PREV_PROOF_ARGS="--prev-proof $PREV_PROOF_FILE --prev-vk $PREV_VK_FILE"
    fi
fi

if [ "$MODE" == "--prove" ]; then
    echo "🚀 Running in PROVE mode (this will take time!)"
    if [ -n "$GROTH16" ]; then
        if [ "$USE_NETWORK" = true ]; then
            echo "📦 Groth16 mode enabled (using SP1 Network)"
        else
            echo "📦 Groth16 mode enabled (requires Docker locally)"
        fi
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
    # Use the SP1 toolchain rustc found earlier
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
ELF_PATH="$ELF" "$HOST_BINARY" $MODE $GROTH16 --input-file "$INPUT_FILE" $PREV_PROOF_ARGS

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
