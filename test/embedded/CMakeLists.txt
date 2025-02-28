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

# Explicitly ensure startup.s uses A-profile flags
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/startup.s PROPERTIES 
    LANGUAGE ASM
    COMPILE_FLAGS "${ASM_CORTEX_A_FLAGS}"
)

target_include_directories(minimal_verify.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Set consistent architecture flags for all files
target_compile_options(minimal_verify.elf PRIVATE -mcpu=cortex-a15 -mthumb)
set_target_properties(minimal_verify.elf PROPERTIES
    COMPILE_FLAGS "-mcpu=cortex-a15 -mthumb"
)

target_link_libraries(minimal_verify.elf
    verifier
    ${EMBEDDED_SYS_LIBS}
)

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

# Bare-metal test with minimal dependencies
add_executable(baremetal_test.elf 
    baremetal_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/baremetal_startup.s
)

# Explicitly set language properties for the assembly file
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/baremetal_startup.s PROPERTIES 
    LANGUAGE ASM
    COMPILE_FLAGS "${ASM_CORTEX_M_FLAGS}"
)

# Set consistent architecture flags for all files
target_compile_options(baremetal_test.elf PRIVATE -g -mcpu=cortex-m3 -mthumb -nostdlib)

# Ensure the target properties are consistent
set_target_properties(baremetal_test.elf PROPERTIES
    COMPILE_FLAGS "-mcpu=cortex-m3 -mthumb"
)

target_link_options(baremetal_test.elf PRIVATE 
    -nostdlib 
    -T${CMAKE_CURRENT_SOURCE_DIR}/baremetal_linker.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/baremetal_test.map
    -mcpu=cortex-m3 -mthumb
)
target_link_libraries(baremetal_test.elf PRIVATE c gcc)

# Add a custom command to print the size of the binary
add_custom_command(TARGET baremetal_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/baremetal_test.elf
    COMMENT "Size of baremetal_test.elf"
)

# Add a target for Cortex-M3 MPS2 test
add_executable(mps2_test.elf 
    baremetal_test.c
    mps2_startup.s
)

# Explicitly set language properties for the assembly file
set_source_files_properties(mps2_startup.s PROPERTIES LANGUAGE ASM)

# Set consistent architecture flags for all files
target_compile_options(mps2_test.elf PRIVATE -g -mcpu=cortex-m3 -mthumb -nostdlib)
set_target_properties(mps2_test.elf PROPERTIES
    COMPILE_FLAGS "-mcpu=cortex-m3 -mthumb"
)

target_link_options(mps2_test.elf PRIVATE 
    -nostdlib 
    -T${CMAKE_CURRENT_SOURCE_DIR}/mps2_linker.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/mps2_test.map
    -mcpu=cortex-m3 -mthumb
)
target_link_libraries(mps2_test.elf PRIVATE c gcc)

# Add a custom command to print the size of the binary
add_custom_command(TARGET mps2_test.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_CURRENT_BINARY_DIR}/mps2_test.elf
    COMMENT "Size of mps2_test.elf"
) 
