## Toolchain file for ARM cross-compilation

# Target system
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Set the target specific executable extension
set(CMAKE_EXECUTABLE_SUFFIX .elf)

# Find the ARM GCC toolchain
find_program(ARM_GCC arm-none-eabi-gcc)
find_program(ARM_CXX arm-none-eabi-g++)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
find_program(ARM_SIZE arm-none-eabi-size)

if(NOT ARM_GCC)
    message(FATAL_ERROR "ARM GCC toolchain not found. Please install arm-none-eabi-gcc.")
endif()

# Set the compilers
set(CMAKE_C_COMPILER ${ARM_GCC})
set(CMAKE_CXX_COMPILER ${ARM_CXX})
set(CMAKE_ASM_COMPILER ${ARM_GCC})
set(CMAKE_SIZE ${ARM_SIZE})

# Don't try to link during configure stage
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Default to Cortex-A15 for A-profile targets
set(ARM_ARCH_FLAGS_A "-mcpu=cortex-a15 -mthumb")
# M-profile flags for Cortex-M3
set(ARM_ARCH_FLAGS_M "-mcpu=cortex-m3 -mthumb")

# If using M-profile assembly, set proper flags
if(EMBEDDED_ASM_M_PROFILE)
    # Set M-profile flags for assembly
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARM_ARCH_FLAGS_M}")
    
    # For explicitly setting assembly files with M-profile
    function(set_assembly_m_profile target_file)
        set_source_files_properties(${target_file} PROPERTIES 
            LANGUAGE ASM
            COMPILE_FLAGS "${ARM_ARCH_FLAGS_M}")
    endfunction()
    
    # For explicitly setting assembly files with A-profile
    function(set_assembly_a_profile target_file)
        set_source_files_properties(${target_file} PROPERTIES 
            LANGUAGE ASM
            COMPILE_FLAGS "${ARM_ARCH_FLAGS_A}")
    endfunction()
else()
    # Default to A-profile for assembly
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARM_ARCH_FLAGS_A}")
    
    # For explicitly setting assembly files with M-profile
    function(set_assembly_m_profile target_file)
        set_source_files_properties(${target_file} PROPERTIES 
            LANGUAGE ASM
            COMPILE_FLAGS "${ARM_ARCH_FLAGS_M}")
    endfunction()
    
    # For explicitly setting assembly files with A-profile
    function(set_assembly_a_profile target_file)
        set_source_files_properties(${target_file} PROPERTIES 
            LANGUAGE ASM
            COMPILE_FLAGS "${ARM_ARCH_FLAGS_A}")
    endfunction()
endif()

# Set compiler flags for ARM embedded
set(CMAKE_C_FLAGS_INIT "-g -O2 ${ARM_ARCH_FLAGS_A}")
set(CMAKE_CXX_FLAGS_INIT "-g -O2 ${ARM_ARCH_FLAGS_A}")

# Skip linking stage during compilation
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# Important build flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-builtin -ffunction-sections -fdata-sections -nostartfiles")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin -ffunction-sections -fdata-sections -nostartfiles")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -static -nostartfiles")

# Debugging options
set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Build type")

# Add target-specific definitions
add_definitions(-DC4_STATIC_MEMORY)
add_definitions(-DBLST_PORTABLE)
add_definitions(-D__STDC_FORMAT_MACROS)
add_definitions(-D_POSIX_C_SOURCE=200809L)
#is this needed?
add_definitions(-DEMBEDDED)