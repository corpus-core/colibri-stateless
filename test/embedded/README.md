: Building

:: Embedded

This directory contains the necessary files to build and test the Colibri verifier for embedded systems.

## Memory Requirements

The Colibri verifier's memory footprint depends on build configuration:

### Default Embedded Build
- **Flash/ROM**: ~150 KB (with USE_PRECOMPUTED_CP=0, printf removal)
- **RAM**: ~108 KB minimum, 128 KB recommended
  - 8 KB for static data (BSS)
  - 15 KB for proof data (heap)
  - 8 KB for BLST cryptographic operations
  - 50 KB for BLS keys
  - ~27 KB for stack and other runtime allocations

### Optimized Build (PRECOMPILE_ZERO_HASHES=OFF, WEAK_SUBJECTIVITY_CHECK=OFF)
- **Flash/ROM**: ~147-148 KB (additional ~3 KB savings)
- **RAM**: ~107 KB minimum (saves ~1 KB from zero hash cache)

During key updates, an additional ~25 KB of RAM might be temporarily needed (or 0 KB if BLS_DESERIALIZE=OFF).

## Supported Hardware

The Colibri verifier has been tested and designed to work on:

- **CPU**: ARM Cortex-A15 and compatible (current CI/testing target)
- **Recommended MCUs**: 
  - ARM Cortex-M4/M7 based microcontrollers with at least 256KB Flash and 128KB RAM
  - Examples: STM32F4 series, NXP LPC4000 series, TI TM4C series

## Project Structure

### Essential Files (Used in CI)
- `startup.s`: Startup code for the ARM Cortex-A15
- `linker_minimal.ld`: Linker script for the embedded build
- `syscalls.c`: System call implementations for embedded environment
- `minimal_verify.c`: Minimal test for memory requirements and verification functionality
- `verify_embedded.c`: Full verification example with file I/O
- `semihosting_test.c`: Test of ARM semihosting functionality
- `toolchain.cmake`: CMake toolchain file for ARM cross-compilation
- `Dockerfile.embedded`: Docker configuration for building and testing
- `run_embedded_test.sh`: Script to run the full embedded test
- `run_semihosting_test.sh`: Script to run the semihosting test

### Build Configuration
- `CMakeLists.txt`: CMake build configuration for all embedded targets

## CI/Testing

The project is now integrated into the main CMake workflow in `.github/workflows/cmake.yml`:

1. **Embedded Build Analysis**:
   - Builds the full verifier and minimal verifier for ARM Cortex-A15
   - Analyzes memory usage and creates detailed reports in GitHub step summaries
   - Produces artifacts with symbol tables and section information

2. **Embedded Tests**:
   - Are being integrated in the Embedded Test workflow
   - Run minimal_verify.elf in QEMU with semihosting enabled
   - Test memory allocation and basic verification functionality

## Building Locally

There are two ways to build the embedded targets:

### Using Docker (Recommended)

This is the most reliable method as it ensures all required tools are available:

```bash
# Build the Docker image
docker build -t c4-embedded -f test/embedded/Dockerfile.embedded .

# Build all embedded targets (with optimizations)
docker run --rm -v $(pwd):/workspace c4-embedded bash -c "cd /workspace && \
  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/workspace/test/embedded/toolchain.cmake \
  -DEMBEDDED=ON -DCMAKE_BUILD_TYPE=MinSizeRel -DCURL=OFF -DPROVER=OFF -DCLI=OFF \
  -DPRECOMPILE_ZERO_HASHES=OFF -DWEAK_SUBJECTIVITY_CHECK=OFF \
  -DINCLUDE=test/embedded && \
  cmake --build build --target verify_embedded.elf minimal_verify.elf semihosting_test.elf"

# Run the embedded verification test
./test/embedded/run_embedded_test.sh

# Run the semihosting test
./test/embedded/run_semihosting_test.sh
```

### Direct Build (Requires ARM Toolchain)

If you have the ARM toolchain installed locally:

```bash
# Create build directory and configure (with optimizations)
mkdir -p build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$PWD/test/embedded/toolchain.cmake \
  -DEMBEDDED=ON -DCMAKE_BUILD_TYPE=MinSizeRel -DCURL=OFF -DPROVER=OFF -DCLI=OFF \
  -DPRECOMPILE_ZERO_HASHES=OFF -DWEAK_SUBJECTIVITY_CHECK=OFF \
  -DINCLUDE=test/embedded

# Build targets
cmake --build build --target verify_embedded.elf minimal_verify.elf semihosting_test.elf
```

## Directory Maintenance

To clean up unused files:

```bash
# Run the cleanup script to move non-essential files to backup
./test/embedded/cleanup.sh

# Delete backup files if everything works correctly
rm -rf test/embedded/backup
```

## Memory Optimization Guide

### Binary Size Optimization Flags

The Colibri verifier provides several CMake flags to reduce binary size for embedded devices. Configure these based on your application requirements:

#### 1. Feature Selection Flags

| Flag | Default | Binary Impact | Description |
|------|---------|---------------|-------------|
| `ETH_BLOCK` | ON | ~varies | Enable block verification APIs (`eth_getBlockByHash`, etc.) |
| `ETH_TX` | ON | ~varies | Enable transaction verification APIs |
| `ETH_RECEIPT` | ON | ~varies | Enable receipt verification APIs |
| `ETH_LOGS` | ON | ~varies | Enable log verification APIs |
| `ETH_CALL` | ON | ~varies | Enable eth_call verification (requires EVM) |
| `ETH_ACCOUNT` | ON | ~varies | Enable account verification APIs |
| `EVMONE` / `EVMLIGHT` | ON / OFF | Large | EVM implementations (disable both if no eth_call needed) |

**Recommendation**: Only enable the APIs your application actually uses.

#### 2. PRECOMPILE_ZERO_HASHES (Memory Optimization)

```cmake
-DPRECOMPILE_ZERO_HASHES=OFF  # Saves ~1 KB
```

**What it does**: Disables caching of zero hashes (30×32 bytes = 960 bytes) used for SSZ Merkle tree calculations.

**When to disable**:
- ✅ When `ETH_BLOCK=OFF` (only needed for block body verification)
- ✅ For embedded devices with limited RAM
- ✅ Light client sync-only applications

**Trade-offs**:
- ⚠️ Block verification will compute zero hashes on-demand (slower)
- ✅ Saves 1 KB of RAM
- ✅ No security impact

#### 3. WEAK_SUBJECTIVITY_CHECK (Security vs Size Trade-off)

```cmake
-DWEAK_SUBJECTIVITY_CHECK=OFF  # Saves ~2-3 KB
```

**What it does**: Disables weak subjectivity period validation when syncing after extended offline periods (>2 weeks).

**⚠️ SECURITY WARNING**: This increases the risk of long-range attacks!

**When to disable**:
- ✅ Embedded devices **without HTTP access** (e.g., Bluetooth-only)
- ✅ Applications protecting **small values** (attack cost >> protected value)
- ✅ Devices that sync frequently (< 2 week offline periods)

**When to KEEP enabled**:
- 🛡️ Devices with internet connectivity
- 🛡️ High-value applications
- 🛡️ Devices that may be offline for weeks

**Security Model**:

Without weak subjectivity checks, an attacker who controlled >2/3 of validators in the past could:
1. Wait for validators to exit (avoid slashing)
2. Create a fake alternative chain history
3. Convince your light client to follow the fake chain

**Attack Cost**: Very high (requires past majority stake control), but increases risk.

**Reference**: [Weak Subjectivity Analysis (Runtime Verification)](https://github.com/runtimeverification/beacon-chain-verification/blob/master/weak-subjectivity/weak-subjectivity-analysis.pdf)

**Trade-offs**:
- ⚠️ Increases long-range attack risk
- ✅ Saves ~2-3 KB of code
- ✅ No HTTP dependency needed

#### 4. BLS_DESERIALIZE (Memory vs Speed)

```cmake
-DBLS_DESERIALIZE=OFF  # Saves ~25 KB RAM per cached period
```

**What it does**: Stores BLS keys in compressed format instead of deserialized.

**Trade-offs**:
- ⚠️ Slower signature verification (needs deserialization on each use)
- ✅ Saves ~25 KB RAM per sync committee period
- ✅ Recommended for embedded (default for EMBEDDED=ON)

### Example Configurations

#### Minimal Light Client (Sync Only, No HTTP)
```cmake
cmake -B build \
  -DEMBEDDED=ON \
  -DETH_BLOCK=OFF \
  -DETH_TX=ON \
  -DETH_RECEIPT=OFF \
  -DETH_LOGS=OFF \
  -DETH_CALL=OFF \
  -DETH_ACCOUNT=OFF \
  -DEVMONE=OFF \
  -DPRECOMPILE_ZERO_HASHES=OFF \
  -DWEAK_SUBJECTIVITY_CHECK=OFF \
  -DCURL=OFF
```
**Savings**: ~3-4 KB vs default embedded build

#### Full Verifier with Security (HTTP-enabled)
```cmake
cmake -B build \
  -DEMBEDDED=ON \
  -DETH_BLOCK=OFF \
  -DETH_TX=ON \
  -DETH_CALL=OFF \
  -DEVMONE=OFF \
  -DPRECOMPILE_ZERO_HASHES=OFF \
  -DWEAK_SUBJECTIVITY_CHECK=ON \  # Keep security check
  -DCURL=ON
```
**Savings**: ~1 KB vs default, keeps security

### Additional Optimization Tips

1. **Compiler Flags**: Use `-DCMAKE_BUILD_TYPE=MinSizeRel` for size optimization
2. **Linker Flags**: Enable `--gc-sections` to remove unused code
3. **Static Allocation**: Use `STATIC_MEMORY=ON` (default for EMBEDDED)
4. **BLS Keys**: Store in flash when possible, only load to RAM when needed
5. **Proof Processing**: Process large proofs in chunks if memory constrained 