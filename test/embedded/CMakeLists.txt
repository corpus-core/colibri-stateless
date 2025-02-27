# Embedded test configuration
cmake_minimum_required(VERSION 3.10)

# Set up ARM cross-compilation
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Static memory configuration
if(STATIC_MEMORY)
    add_definitions(-DC4_STATIC_MEMORY)
    # Define static buffer sizes
    add_definitions(-DC4_STATIC_STATE_SIZE=1024)    # 1KB for state
    add_definitions(-DC4_STATIC_SYNC_SIZE=49152)    # 48KB for sync committee
endif()

# Toolchain settings
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Compiler flags for embedded target
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections -fno-common -fno-builtin")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# Use newlib for filesystem support
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs --specs=rdimon.specs")

# Configure blst for embedded target (will be used by util/crypto)
set(BLS_SIGNATURES_BUILD_SHARED_LIBS OFF)
set(BLS_SIGNATURES_USE_STATIC ON)
add_definitions(-DBLST_PORTABLE)  # Force portable C implementation

# Create embedded test executable
add_executable(verify_embedded verify_embedded.c)

# Include directories
target_include_directories(verify_embedded PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
)

# Link with verifier library (which brings in util, crypto, and blst)
target_link_libraries(verify_embedded verifier)

# Add linker flags for embedded target
target_link_options(verify_embedded PRIVATE
    -specs=nano.specs
    -specs=nosys.specs  # Use this instead of rdimon.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# Add startup code and linker script
target_sources(verify_embedded PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
)

# Set binary format
set_target_properties(verify_embedded PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cortex-m4.ld
)

# Add memory analysis targets
add_custom_target(size_analysis
    COMMAND arm-none-eabi-size --format=berkeley $<TARGET_FILE:verify_embedded>
    DEPENDS verify_embedded
    COMMENT "Analyzing embedded binary size"
)

add_custom_target(memory_map
    COMMAND arm-none-eabi-nm --print-size --size-sort --radix=d $<TARGET_FILE:verify_embedded> > memory_map.txt
    DEPENDS verify_embedded
    COMMENT "Generating memory map"
) 