## Toolchain file for ARM cross-compilation

# Target system
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Cross compiler tools
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_STRIP arm-none-eabi-strip)

# Important build flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-builtin -ffunction-sections -fdata-sections -nostartfiles -nostdlib -specs=nosys.specs")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin -ffunction-sections -fdata-sections -nostartfiles -nostdlib -specs=nosys.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -static -nostartfiles")

# Debugging options
set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Build type")

# Processor specific flags
set(CPU_FLAGS "-mcpu=cortex-a15 -mthumb")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}")

# Add target-specific definitions
add_definitions(-DSTATIC_MEMORY)
add_definitions(-DBLST_PORTABLE)
add_definitions(-DBLS_DESERIALIZE)

# Prevent trying to find the compiler automatically
# since it won't find the cross-compiler otherwise
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Don't run the linker test 
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)