#!/bin/bash
set -e

# Defaults
PERIOD=""
PREV_PERIOD=""
START_PERIOD=""
END_PERIOD=""
MODE="--execute"
OUTPUT_DIR="build/default/.period_store"
REMOTE_URL="${REMOTE_URL:-https://mainnet1.colibri-proof.tech/}"
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

# Load private keys from files (useful for Docker secrets)
if [ -z "$SP1_PRIVATE_KEY" ] && [ -z "$PRIVATE_KEY_ARG" ]; then
    if [ -z "$SP1_PRIVATE_KEY_FILE" ] && [ -f "/run/secrets/sp1_private_key" ]; then
        SP1_PRIVATE_KEY_FILE="/run/secrets/sp1_private_key"
    fi
    if [ -n "$SP1_PRIVATE_KEY_FILE" ] && [ -f "$SP1_PRIVATE_KEY_FILE" ]; then
        export SP1_PRIVATE_KEY="$(tr -d ' \r\n\t' < "$SP1_PRIVATE_KEY_FILE")"
    fi
fi

if [ -z "$NETWORK_PRIVATE_KEY" ]; then
    if [ -z "$NETWORK_PRIVATE_KEY_FILE" ] && [ -f "/run/secrets/network_private_key" ]; then
        NETWORK_PRIVATE_KEY_FILE="/run/secrets/network_private_key"
    fi
    if [ -n "$NETWORK_PRIVATE_KEY_FILE" ] && [ -f "$NETWORK_PRIVATE_KEY_FILE" ]; then
        export NETWORK_PRIVATE_KEY="$(tr -d ' \r\n\t' < "$NETWORK_PRIVATE_KEY_FILE")"
    fi
fi

if [ -z "$NETWORK_PRIVATE_KEY" ] && [ -n "$SP1_PRIVATE_KEY" ]; then
    export NETWORK_PRIVATE_KEY="$SP1_PRIVATE_KEY"
fi

# --- SP1 NETWORK SETUP ---
if [ "$USE_NETWORK" = true ]; then
    export SP1_PROVER="network"
    if [ -n "$PRIVATE_KEY_ARG" ]; then
        export SP1_PRIVATE_KEY="$PRIVATE_KEY_ARG"
        export NETWORK_PRIVATE_KEY="$PRIVATE_KEY_ARG"
    fi

    if [ -z "$SP1_PRIVATE_KEY" ]; then
        echo "❌ Error: Network mode selected but SP1_PRIVATE_KEY not found."
        echo "   Please provide it via --private-key <key> or export SP1_PRIVATE_KEY=<key>"
        exit 1
    fi
    echo "🌐 SP1 Network Mode: ENABLED"
fi

# Check for pre-built artifacts (Docker/Production mode)
SKIP_TOOLCHAIN=false
if [ -f "/app/eth_sync_program" ] && [ -f "/app/eth-sync-script" ]; then
    echo "🐳 Docker Environment Detected: Using pre-built artifacts"
    ELF="/app/eth_sync_program"
    HOST_BINARY="/app/eth-sync-script"
    SKIP_TOOLCHAIN=true
else
    # Prefer artifacts next to colibri-server (local build output)
    # This matches the server integration which resolves via uv_exepath() directory.
    if [ -z "$HOST_BINARY" ] && [ -f "build/default/bin/eth-sync-script" ]; then
        HOST_BINARY="build/default/bin/eth-sync-script"
    fi
    if [ -z "$ELF" ] && [ -f "build/default/bin/eth_sync_program" ]; then
        ELF="build/default/bin/eth_sync_program"
    fi
fi

# --- SP1 TOOLCHAIN SETUP ---

if [ "$SKIP_TOOLCHAIN" = false ]; then
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
    # aqFpu2ZKYP corresponds to the sp1 v6.3.0 toolchain (rustc 1.94.0-dev),
    # installed via `sp1up -v v6.3.0`. The previous v5 pin was "PkFc33VNGO".
    # NOTE: the toolchain directory name is assigned at install time; on a machine where
    # this exact name is missing the script falls back to the latest installed toolchain
    # (with a loud warning). For reproducible VKs always install via `sp1up -v v6.3.0`.
    PINNED_TOOLCHAIN="aqFpu2ZKYP"
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
fi

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

# Write SP1 balance to a predictable file for monitoring (best-effort).
# The Rust host binary will only use this when SP1_PROVER=network.
export SP1_BALANCE_FILE="${OUTPUT_DIR%/}/sp1_balance.txt"

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
    
    # Explicitly define compressed intermediates (needed for recursion)
    PROOF_COMPRESSED_FILE="$OUTPUT_DIR_ABS/zk_proof.bin"
    VK_COMPRESSED_FILE="$OUTPUT_DIR_ABS/zk_vk_raw.bin"
else
    PROOF_FILE="$OUTPUT_DIR_ABS/zk_proof.bin"
    VK_FILE="$OUTPUT_DIR_ABS/zk_vk_raw.bin"
    PUBLIC_VALUES_FILE="$OUTPUT_DIR_ABS/zk_pub.bin"
    
    # Same as main output
    PROOF_COMPRESSED_FILE="$PROOF_FILE"
    VK_COMPRESSED_FILE="$VK_FILE"
fi

# Fetch Input Data if missing
if [ ! -f "$INPUT_FILE" ]; then
    echo "📥 Fetching sync data for period $PERIOD..."
    curl -X POST "$REMOTE_URL" \
         -H "content-type: application/json" \
         -d "{\"method\":\"eth_proof_sync\",\"params\":[$PERIOD],\"id\":1,\"jsonrpc\":\"2.0\"}" \
         --output "$INPUT_FILE" 
    
    # Check if file is valid (minimum size)
    if [ ! -f "$INPUT_FILE" ]; then
        echo "❌ Failed to fetch data or empty response (no file created)"
        exit 1
    fi

    FILE_SIZE_BYTES=$(wc -c < "$INPUT_FILE" | tr -d '[:space:]')

    # Very small response (< 1 kB) is likely just an error message: show it and abort
    if [ "$FILE_SIZE_BYTES" -lt 1024 ]; then
        echo "❌ Failed to fetch data: response too small (${FILE_SIZE_BYTES} bytes, likely error message)"
        echo "---- Response from server ----"
        cat "$INPUT_FILE"
        echo "------------------------------"
        rm -f "$INPUT_FILE"
        exit 1
    fi

    # Below 25 kB we consider the payload invalid and abort without using it
    MIN_VALID_SIZE_BYTES=$((25 * 1024))
    if [ "$FILE_SIZE_BYTES" -lt "$MIN_VALID_SIZE_BYTES" ]; then
        echo "❌ Failed to fetch data: response too small (${FILE_SIZE_BYTES} bytes, expected at least ${MIN_VALID_SIZE_BYTES} bytes)"
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

if [ "$SKIP_TOOLCHAIN" = false ]; then
    # Workspace Root relative to this script (scripts/ -> ./)
    WORKSPACE_ROOT=$(cd "$(dirname "$0")/.." && pwd)

    # Frozen ELF Path
    ELF_DIR="$WORKSPACE_ROOT/src/chains/eth/zk_proof/program/elf"
    ELF_FROZEN="$ELF_DIR/eth_sync_program"

    if [ -f "$ELF_FROZEN" ]; then
        echo "🧊 Using FROZEN Guest ELF: $ELF_FROZEN"
        echo "   (Skipping guest build to ensure stable Verification Key)"
        ELF="$ELF_FROZEN"
    else
        # Build Guest.
        # SP1 v6 uses the 64-bit RISC-V target (riscv64im-succinct-zkvm-elf). We rely on
        # `cargo prove build`, which selects the correct target, linker arguments and the
        # `+succinct` toolchain automatically (the old manual `cargo build --target
        # riscv32im-...` with hardcoded RUSTFLAGS is v5-only and no longer valid).
        echo "🔨 Building Guest Program (cargo prove build)..."
        (
            unset RUSTFLAGS
            unset RUSTC
            cd "$WORKSPACE_ROOT/src/chains/eth/zk_proof/program"
            cargo prove build
        )

        ELF="$WORKSPACE_ROOT/src/chains/eth/zk_proof/target/elf-compilation/riscv64im-succinct-zkvm-elf/release/eth-sync-program"

        if [ ! -f "$ELF" ]; then
            echo "❌ Error: Could not find guest ELF binary at $ELF"
            exit 1
        fi

        echo "✅ Built ELF: $ELF"

        # Save to frozen path for next time / git commit
        echo "💾 Saving ELF to $ELF_FROZEN"
        echo "   ⚠️  IMPORTANT: Commit this file to git to freeze the Verification Key!"
        cp "$ELF" "$ELF_FROZEN"
        ELF="$ELF_FROZEN"
    fi

    echo "✅ Using ELF: $ELF"

    # Build Host
    echo "🔨 Building Host Script..."
    unset RUSTFLAGS
    unset RUSTC

    # Optimized CPU flags for Apple Silicon / Native
    if [[ "$OSTYPE" == "darwin"* && $(uname -m) == "arm64" ]]; then
         export RUSTFLAGS="-C target-cpu=native"
    fi

    cd "$WORKSPACE_ROOT/src/chains/eth/zk_proof/script"
    cargo build --release

    # Run Host
    echo "🏃 Running Host Script..."
    HOST_BINARY="$WORKSPACE_ROOT/src/chains/eth/zk_proof/target/release/eth-sync-script"
else 
    echo "✅ Using Pre-built ELF: $ELF"
    echo "✅ Using Pre-built Host Binary: $HOST_BINARY"
fi

if [ ! -f "$HOST_BINARY" ]; then
    echo "❌ Error: Host binary not found at $HOST_BINARY"
    exit 1
fi

# Pass Output Path via Env Var
export PROOF_OUTPUT_FILE="$PROOF_FILE"
export VK_OUTPUT_FILE="$VK_FILE"
export PUBLIC_VALUES_FILE="$PUBLIC_VALUES_FILE"
export PROOF_COMPRESSED_OUTPUT_FILE="$PROOF_COMPRESSED_FILE"
export VK_COMPRESSED_OUTPUT_FILE="$VK_COMPRESSED_FILE"

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
