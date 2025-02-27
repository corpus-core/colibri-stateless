#!/bin/bash
set -e

# Get the project root directory (two levels up from this script)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

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

    # Configure CMake with absolute paths
    cmake -B build -S . \
      -DCMAKE_TOOLCHAIN_FILE=/workspace/test/embedded/toolchain.cmake \
      -DEMBEDDED=ON \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCURL=OFF \
      -DPROOFER=OFF \
      -DCLI=OFF \
      -DINCLUDE=test/embedded

    # Build
    cmake --build build

    # Create SD card image in the embedded test directory
    cd /workspace/test/embedded
    dd if=/dev/zero of=sdcard.img bs=1M count=64
    mkfs.vfat sdcard.img

    # Copy test files using absolute paths
    mcopy -i sdcard.img /workspace/test/data/eth_getLogs1/states_1 ::/states_1
    mcopy -i sdcard.img /workspace/test/data/eth_getLogs1/proof.ssz ::/proof.ssz
    mcopy -i sdcard.img /workspace/test/data/eth_getLogs1/sync_1_1351 ::/sync_1_1351

    # Show contents
    echo "SD card contents:"
    mdir -i sdcard.img

    # Run size analysis from the build directory
    cd /workspace/build/test/embedded
    arm-none-eabi-size --format=berkeley verify_embedded.elf
    arm-none-eabi-nm --print-size --size-sort --radix=d verify_embedded.elf > memory_map.txt
    cat memory_map.txt

    # Run in QEMU (from the embedded test directory)
    cd /workspace/test/embedded
    qemu-system-arm \
      -M mps2-an385 \
      -cpu cortex-m3 \
      -nographic \
      -semihosting \
      -kernel /workspace/build/test/embedded/verify_embedded.elf \
      -drive file=sdcard.img,if=sd,format=raw
'