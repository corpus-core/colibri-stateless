# Copyright (c) 2025 corpus.core
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

# Embedded test configuration
cmake_minimum_required(VERSION 3.10)

# Option for M-profile assembly files (Cortex-M3)
option(EMBEDDED_ASM_M_PROFILE "Use Cortex-M profile for assembly files" OFF)

# Enable assembly language 
enable_language(ASM)

# Set ARM CPU flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a15 -mthumb -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a15 -mthumb -Os")

# Set ASM flags based on profile option
if(EMBEDDED_ASM_M_PROFILE)
  # Use Cortex-M3 for assembly by default
  set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=cortex-m3 -mthumb")
  set(ASM_CORTEX_A_FLAGS "-mcpu=cortex-a15 -mthumb")
  set(ASM_CORTEX_M_FLAGS "-mcpu=cortex-m3 -mthumb")
  message(STATUS "Using Cortex-M3 profile for assembly files by default")
else()
  # Use Cortex-A15 for assembly by default
  set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=cortex-a15 -mthumb")
  set(ASM_CORTEX_A_FLAGS "-mcpu=cortex-a15 -mthumb")
  set(ASM_CORTEX_M_FLAGS "-mcpu=cortex-m3 -mthumb")
  message(STATUS "Using Cortex-A15 profile for assembly files by default")
endif()

# Set startup.s as assembly file explicitly for A-profile
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/startup.s PROPERTIES 
    LANGUAGE ASM
    COMPILE_FLAGS "${ASM_CORTEX_A_FLAGS}"
)

# Set mps2_startup.s as assembly file explicitly for M-profile
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mps2_startup.s PROPERTIES 
    LANGUAGE ASM
    COMPILE_FLAGS "${ASM_CORTEX_M_FLAGS}"
)

# Set baremetal_startup.s as assembly file explicitly for M-profile
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/baremetal_startup.s PROPERTIES 
    LANGUAGE ASM
    COMPILE_FLAGS "${ASM_CORTEX_M_FLAGS}"
)

# Common embedded source files
set(EMBEDDED_COMMON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/startup.s
    ${CMAKE_CURRENT_SOURCE_DIR}/syscalls.c
)

# Common system libraries for embedded targets
set(EMBEDDED_SYS_LIBS
    -lgcc
    -lc
    -lm
)

# Minimal verification executable
add_executable(minimal_verify.elf
    ${EMBEDDED_COMMON_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/minimal_verify.c
)

# Use our helper function to explicitly set assembly file to A-profile
if(COMMAND set_assembly_a_profile)
    set_assembly_a_profile(${CMAKE_CURRENT_SOURCE_DIR}/startup.s)
else()
    # Fallback if function not available
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/startup.s PROPERTIES 
        LANGUAGE ASM
        COMPILE_FLAGS "-mcpu=cortex-a15 -mthumb"
    )
endif()

target_include_directories(minimal_verify.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Set consistent architecture flags for C files
target_compile_options(minimal_verify.elf PRIVATE 
    -g 
    -mcpu=cortex-a15 
    -mthumb
)

# Link with verification libraries
target_link_libraries(minimal_verify.elf
    verifier
    ${EMBEDDED_SYS_LIBS}
)

# Use explicit linker flags to ensure A-profile architecture is used
set_target_properties(minimal_verify.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/linker_minimal.ld -Wl,-Map=minimal_verify.map --specs=nosys.specs -mcpu=cortex-a15 -mthumb"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET minimal_verify.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/minimal_verify.elf
    COMMENT "Size of minimal_verify.elf"
)

# Simple semihosting test executable
add_executable(semihosting_test.elf
    ${EMBEDDED_COMMON_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/semihosting_test.c
)

target_include_directories(semihosting_test.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(semihosting_test.elf PROPERTIES
    LINK_FLAGS "-Wl,--gc-sections -static -nostartfiles -Wl,--gc-sections -T${CMAKE_CURRENT_SOURCE_DIR}/linker_minimal.ld -Wl,-Map=semihosting_test.map --specs=nosys.specs"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET semihosting_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/semihosting_test.elf
    COMMENT "Size of semihosting_test.elf"
)

# Original verification executable (kept for compatibility)
add_executable(verify_embedded.elf
    ${EMBEDDED_COMMON_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/verify_embedded.c
)

target_include_directories(verify_embedded.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(verify_embedded.elf
    verifier
    ${EMBEDDED_SYS_LIBS}
)

set_target_properties(verify_embedded.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/linker_minimal.ld -Wl,-Map=verify_embedded.map --specs=nosys.specs"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET verify_embedded.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/verify_embedded.elf
    COMMENT "Size of verify_embedded.elf"
)

# Simple test for QEMU debugging
add_executable(simple_test.elf
    ${EMBEDDED_COMMON_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_test.c
)

set_target_properties(simple_test.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/linker_minimal.ld -Wl,-Map=simple_test.map --specs=nosys.specs"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET simple_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/simple_test.elf
    COMMENT "Size of simple_test.elf"
)

# Create a separate object library for the M-profile assembly file
add_library(baremetal_startup_m OBJECT EXCLUDE_FROM_ALL)
target_sources(baremetal_startup_m PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/baremetal_startup.s)
# Force M-profile flags for this assembly file
set_target_properties(baremetal_startup_m PROPERTIES
    COMPILE_FLAGS "-mcpu=cortex-m3 -mthumb"
)

# Bare-metal test without assembly source (will link with object lib)
add_executable(baremetal_test.elf baremetal_test.c)

# Set architecture flags for C file
target_compile_options(baremetal_test.elf PRIVATE 
    -g 
    -mcpu=cortex-m3 
    -mthumb 
    -nostdlib
)

# Link with our custom-compiled assembly object
target_link_libraries(baremetal_test.elf PRIVATE 
    baremetal_startup_m
    -nostdlib 
    -lgcc -lc
)

# Set explicit link flags for M-profile
set_target_properties(baremetal_test.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/baremetal_linker.ld -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/baremetal_test.map -mcpu=cortex-m3 -mthumb"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET baremetal_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/baremetal_test.elf
    COMMENT "Size of baremetal_test.elf"
)

# Create a separate object library for the M-profile assembly file
add_library(mps2_startup_m OBJECT EXCLUDE_FROM_ALL)
target_sources(mps2_startup_m PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mps2_startup.s)
# Force M-profile flags for this assembly file
set_target_properties(mps2_startup_m PROPERTIES
    COMPILE_FLAGS "-mcpu=cortex-m3 -mthumb"
)

# MPS2 test without assembly source (will link with object lib)
add_executable(mps2_test.elf baremetal_test.c)

# Set architecture flags for C file
target_compile_options(mps2_test.elf PRIVATE 
    -g 
    -mcpu=cortex-m3 
    -mthumb 
    -nostdlib
)

# Link with our custom-compiled assembly object
target_link_libraries(mps2_test.elf PRIVATE 
    mps2_startup_m
    -nostdlib 
    -lgcc -lc
)

# Set explicit link flags for M-profile
set_target_properties(mps2_test.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/mps2_linker.ld -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/mps2_test.map -mcpu=cortex-m3 -mthumb"
)

# Add a custom command to print the size of the binary
add_custom_command(TARGET mps2_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/mps2_test.elf
    COMMENT "Size of mps2_test.elf"
) 
