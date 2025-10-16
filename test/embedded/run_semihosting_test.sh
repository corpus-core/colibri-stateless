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
  /bin/bash -c "
    # Configure CMake for embedded build
    echo \"Configuring CMake for embedded build...\"
    cmake -B build -S . \\
      -DCMAKE_TOOLCHAIN_FILE=/workspace/test/embedded/toolchain.cmake \\
      -DEMBEDDED=ON \\
      -DCMAKE_BUILD_TYPE=MinSizeRel \\
      -DCURL=OFF \\
      -DPROVER=OFF \\
      -DCLI=OFF \\
      -DINCLUDE=test/embedded
    
    # Build the semihosting test
    echo \"Building semihosting test...\"
    cmake --build build --target semihosting_test.elf
    
    # Run the semihosting test in QEMU
    echo \"Running the semihosting test in QEMU...\"
    ELF_PATH=\"/workspace/build/test/embedded/semihosting_test.elf\"
    LOG_FILE=\"/workspace/build/semihosting_output.log\"
    
    if [ -f \"\$ELF_PATH\" ]; then
        echo \"Binary size information:\"
        arm-none-eabi-size --format=berkeley \"\$ELF_PATH\"
        
        # Create a very simple QEMU command with enhanced semihosting
        echo \"Starting QEMU with enhanced semihosting...\"
        echo \"---------------------------------------\"
        
        # First, ensure output directory exists
        mkdir -p \$(dirname \"\$LOG_FILE\")
        
        # Run QEMU with semihosting enabled and redirect output to log file
        timeout --foreground 10s qemu-system-arm \\
          -machine virt \\
          -cpu cortex-a15 \\
          -m 128 \\
          -kernel \"\$ELF_PATH\" \\
          -nographic \\
          -semihosting \\
          -semihosting-config enable=on,target=native \\
          -no-reboot > \"\$LOG_FILE\" 2>&1
        
        QEMU_EXIT_CODE=\$?
        echo \"---------------------------------------\"
        
        # Show QEMU exit status
        if [ \$QEMU_EXIT_CODE -eq 124 ]; then
            echo \"QEMU execution timed out after 10 seconds\"
        elif [ \$QEMU_EXIT_CODE -ne 0 ]; then
            echo \"QEMU exited with error code: \$QEMU_EXIT_CODE\"
        else
            echo \"QEMU execution completed successfully\"
        fi
        
        # Display log file contents
        echo \"\"
        echo \"Contents of QEMU output log:\"
        echo \"---------------------------------------\"
        if [ -f \"\$LOG_FILE\" ]; then
            cat \"\$LOG_FILE\"
        else
            echo \"Log file not created\"
        fi
        echo \"---------------------------------------\"
        
        echo \"\"
        echo \"Log file located at: \$LOG_FILE\"
        
    else
        echo \"ERROR: ELF file not found at \$ELF_PATH\"
        exit 1
    fi
" 