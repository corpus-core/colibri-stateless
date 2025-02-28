#!/bin/bash
set -e

# Get the project root directory
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# Build the Docker image
docker build -t c4-embedded -f "${PROJECT_ROOT}/test/embedded/Dockerfile.embedded" "${PROJECT_ROOT}"

# Run the container with the project root mounted
docker run --rm -it \
  -v "${PROJECT_ROOT}:/workspace" \
  -w /workspace \
  c4-embedded \
  /bin/bash -c '
    # Clean any existing build
    rm -rf build

    # Configure CMake with absolute paths and verbose output
    echo "Configuring CMake with verbose output..."
    cmake -B build -S . \
      -DCMAKE_VERBOSE_MAKEFILE=ON \
      -DCMAKE_TOOLCHAIN_FILE=/workspace/test/embedded/toolchain.cmake \
      -DEMBEDDED=ON \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCURL=OFF \
      -DPROOFER=OFF \
      -DCLI=OFF \
      -DINCLUDE=test/embedded

    # Build libraries
    echo "Building libraries..."
    cmake --build build --target crypto blst_lib util verifier
    
    # Manually compile and link the embedded application with startup code
    echo "Manually compiling and linking the embedded application..."
    cd /workspace
    
    # Create output directories
    mkdir -p build/test/embedded/obj
    
    # Compile startup.s
    echo "Compiling startup.s..."
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -mthumb-interwork -mfloat-abi=soft \
      test/embedded/startup.s -o build/test/embedded/obj/startup.o
    
    # Compile C files
    echo "Compiling C files..."
    arm-none-eabi-gcc -DBLST_PORTABLE -DBLS_DESERIALIZE -DSTATIC_MEMORY -I/workspace/src \
      -mcpu=arm926ej-s -mthumb-interwork -mfloat-abi=soft -ffunction-sections -fdata-sections -Os \
      -c test/embedded/verify_embedded.c -o build/test/embedded/obj/verify_embedded.o
    
    arm-none-eabi-gcc -DBLST_PORTABLE -DBLS_DESERIALIZE -DSTATIC_MEMORY -I/workspace/src \
      -mcpu=arm926ej-s -mthumb-interwork -mfloat-abi=soft -ffunction-sections -fdata-sections -Os \
      -c test/embedded/syscalls.c -o build/test/embedded/obj/syscalls.o
    
    # Check the object files
    echo "Checking compiled object files:"
    arm-none-eabi-objdump -h build/test/embedded/obj/startup.o
    arm-none-eabi-objdump -h build/test/embedded/obj/verify_embedded.o
    arm-none-eabi-objdump -h build/test/embedded/obj/syscalls.o
    
    # Link everything together
    echo "Linking the final executable..."
    arm-none-eabi-gcc -mcpu=arm926ej-s -mthumb-interwork -mfloat-abi=soft \
      -ffunction-sections -fdata-sections -Os \
      -nostartfiles -T/workspace/test/embedded/linker_minimal.ld -Wl,--gc-sections \
      build/test/embedded/obj/startup.o \
      build/test/embedded/obj/verify_embedded.o \
      build/test/embedded/obj/syscalls.o \
      -o build/test/embedded/verify_embedded.elf \
      build/src/verifier/libverifier.a \
      build/src/util/libutil.a \
      build/libs/crypto/libcrypto.a \
      build/libs/blst/libblst.a \
      -lgcc -lc -lm -lnosys
    
    # Create directory for test files
    mkdir -p /tmp/embedded_files
    
    # Analyze the ELF file
    ELF_PATH="/workspace/build/test/embedded/verify_embedded.elf"
    if [ -f "$ELF_PATH" ]; then
        echo "Analyzing ELF file:"
        arm-none-eabi-size --format=berkeley "$ELF_PATH"
        
        echo "Symbols in ELF file:"
        arm-none-eabi-nm "$ELF_PATH" | sort
        
        echo "Detailed section headers:"
        arm-none-eabi-readelf -S "$ELF_PATH"
        
        echo "Binary sections and content:"
        arm-none-eabi-objdump -h "$ELF_PATH"
        
        # Copy test files
        cp /workspace/test/data/eth_getLogs1/proof.ssz /tmp/embedded_files/
        cp /workspace/test/data/eth_getLogs1/states_1 /tmp/embedded_files/
        cp /workspace/test/data/eth_getLogs1/sync_1_1351 /tmp/embedded_files/
        
        echo "Files created for testing:"
        ls -la /tmp/embedded_files
        
        # Run QEMU with the ELF file
        echo "Running the embedded application in QEMU..."
        cd /tmp/embedded_files
        
        # Create a log directory
        mkdir -p /tmp/qemu_logs
        
        # Run QEMU with a timeout and more debugging options
        echo "Starting QEMU (with 30 second timeout)..."
        timeout --foreground 30s qemu-system-arm \
          -machine versatilepb \
          -cpu arm926 \
          -m 128 \
          -kernel "$ELF_PATH" \
          -nographic \
          -semihosting \
          -no-reboot \
          -serial stdio \
          -d guest_errors,unimp,in_asm \
          -D /tmp/qemu_logs/qemu_debug.log \
          -monitor none
        
        QEMU_EXIT_CODE=$?
        if [ $QEMU_EXIT_CODE -eq 124 ]; then
            echo "QEMU execution timed out after 30 seconds"
        elif [ $QEMU_EXIT_CODE -ne 0 ]; then
            echo "QEMU exited with error code: $QEMU_EXIT_CODE"
        else
            echo "QEMU execution completed successfully"
        fi
        
        # Display debug logs if available
        if [ -f "/tmp/qemu_logs/qemu_debug.log" ]; then
            echo "=== QEMU Debug Log ==="
            cat /tmp/qemu_logs/qemu_debug.log
            echo "======================"
        else
            echo "No QEMU debug log was captured"
        fi
    else
        echo "ERROR: ELF file not found at $ELF_PATH"
        exit 1
    fi
'