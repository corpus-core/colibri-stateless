# C4 Verifier Embedded Testing

This directory contains the necessary files to build and test the C4 verifier for embedded systems.

## Memory Requirements

The C4 verifier requires the following minimum resources to run:

- **Flash/ROM**: ~225 KB (for code and read-only data)
- **RAM**: ~108 KB minimum, 128 KB recommended
  - 8 KB for static data (BSS)
  - 15 KB for proof data (heap)
  - 8 KB for BLST cryptographic operations
  - 50 KB for BLS keys
  - ~27 KB for stack and other runtime allocations

During key updates, an additional ~25 KB of RAM might be temporarily needed.

## Supported Hardware

The C4 verifier has been tested and designed to work on:

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

# Build all embedded targets
docker run --rm -v $(pwd):/workspace c4-embedded bash -c "cd /workspace && \
  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/workspace/test/embedded/toolchain.cmake \
  -DEMBEDDED=ON -DCMAKE_BUILD_TYPE=MinSizeRel -DCURL=OFF -DPROVER=OFF -DCLI=OFF \
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
# Create build directory and configure
mkdir -p build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$PWD/test/embedded/toolchain.cmake \
  -DEMBEDDED=ON -DCMAKE_BUILD_TYPE=MinSizeRel -DCURL=OFF -DPROVER=OFF -DCLI=OFF \
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

## Memory Optimization Tips

To optimize memory usage in resource-constrained environments:

1. Use static allocation where possible
2. Store BLS keys in flash memory when not updating
3. Process proofs in chunks if they are large
4. Use compiler optimization flags for size (-Os)
5. Enable garbage collection of unused sections (`--gc-sections`) 