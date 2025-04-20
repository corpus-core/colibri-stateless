include(FetchContent)

# Define the output path for the static library
set(BLST_LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/libs/blst)

if(WIN32)
    set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/blst.lib)
else()
    set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/libblst.a)
endif()

# Ensure the output directory exists
file(MAKE_DIRECTORY ${BLST_LIB_OUTPUT_DIR})

# Fetch the blst repository
FetchContent_Declare(
    blst
    GIT_REPOSITORY https://github.com/supranational/blst
    GIT_TAG v0.3.13
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(blst)

# Set up initial flags
if(KOTLIN)
    set(EXTRA_C_FLAGS "-fPIC\ ")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)
if(CMAKE_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")
    # Compiler-Flags für Größe-Optimierung
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections -Os -g0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections -Os -g0")

    # Unterscheide zwischen Plattformen
    if(APPLE)
        # macOS-spezifische Flags
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -dead_strip")
    else()
        # Standard (z. B. für Linux)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --gc-sections")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --gc-sections")
    endif()
endif()

# Handle different platforms and build configurations
if(EMSCRIPTEN)
    set(EMCC emcc)
    # Add a custom command to build the static library for WASM
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${EMCC} -I.. -fexceptions -include stddef.h
                -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=[$intArrayFromString]
                -o ${BLST_LIB}
                -c ${blst_SOURCE_DIR}/src/server.c
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building blst as a static library for WebAssembly with Emscripten"
        VERBATIM
    )
elseif(EMBEDDED)
    # Embedded target configuration
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${CMAKE_COMMAND} -E env 
            CC=${CMAKE_C_COMPILER}
            AR=${CMAKE_AR}
            RANLIB=${CMAKE_RANLIB}
            "CFLAGS=-mcpu=cortex-a15 -mthumb -ffunction-sections -fdata-sections -fno-common -D__BLST_PORTABLE__ ${EXTRA_C_FLAGS}"
            bash ${blst_SOURCE_DIR}/build.sh
        COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building blst for embedded target (ARM Cortex-A15)"
        VERBATIM
    )
elseif(WIN32)
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${CMAKE_COMMAND} -E env CFLAGS="${EXTRA_C_FLAGS}-ffunction-sections -fdata-sections /wd4210" ${blst_SOURCE_DIR}/build.bat -D__BLST_PORTABLE__
        COMMAND ${CMAKE_COMMAND} -E echo "Checking if blst.lib exists"
        COMMAND ${CMAKE_COMMAND} -E copy ${blst_SOURCE_DIR}/blst.lib ${BLST_LIB}
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building and copying blst.lib with optimization flags on Windows"
        VERBATIM
    )
else()
    if(ANDROID)
        # Get the Android NDK paths
        set(ANDROID_NDK_ROOT $ENV{ANDROID_NDK_HOME})
        if(NOT ANDROID_NDK_ROOT)
            set(ANDROID_NDK_ROOT $ENV{ANDROID_NDK})
        endif()
        if(NOT ANDROID_NDK_ROOT)
            set(ANDROID_NDK_ROOT "${ANDROID_NDK}")  # From CMake's Android toolchain
        endif()

        # Determine host platform
        if(CMAKE_HOST_APPLE)
            set(HOST_TAG "darwin-x86_64")
        elseif(CMAKE_HOST_WIN32)
            set(HOST_TAG "windows-x86_64")
        else()
            set(HOST_TAG "linux-x86_64")
        endif()

        # Set up toolchain paths
        set(ANDROID_TOOLCHAIN_PATH "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${HOST_TAG}/bin")
        
        add_custom_command(
            OUTPUT ${BLST_LIB}
            COMMAND ${CMAKE_COMMAND} -E env 
                CC=${ANDROID_TOOLCHAIN_PATH}/clang
                AR=${ANDROID_TOOLCHAIN_PATH}/llvm-ar
                RANLIB=${ANDROID_TOOLCHAIN_PATH}/llvm-ranlib
                CROSS_COMPILE=aarch64-linux-android-
                "CFLAGS=-fPIC -ffunction-sections -fdata-sections --target=${ANDROID_LLVM_TRIPLE}"
                bash ${blst_SOURCE_DIR}/build.sh
            COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
            WORKING_DIRECTORY ${blst_SOURCE_DIR}
            COMMENT "Building blst using build script with Android NDK toolchain"
            VERBATIM
        )
    elseif(SWIFT)
        # iOS build configuration
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            add_custom_command(
                OUTPUT ${BLST_LIB}
                COMMAND ${CMAKE_COMMAND} -E env 
                    CC=${CMAKE_C_COMPILER}
                    AR=${CMAKE_AR}
                    RANLIB=${CMAKE_RANLIB}
                    CROSS_COMPILE=x86_64-apple-ios-simulator
                    "CFLAGS=-target x86_64-apple-ios-simulator -isysroot ${CMAKE_OSX_SYSROOT} -D__BLST_PORTABLE__"
                    bash ${blst_SOURCE_DIR}/build.sh
                COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
                WORKING_DIRECTORY ${blst_SOURCE_DIR}
                COMMENT "Building blst for iOS Simulator (x86_64)"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT ${BLST_LIB}
                COMMAND ${CMAKE_COMMAND} -E env 
                    CC=${CMAKE_C_COMPILER}
                    AR=${CMAKE_AR}
                    RANLIB=${CMAKE_RANLIB}
                    CROSS_COMPILE=arm64-apple-ios
                    "CFLAGS=-target arm64-apple-ios -isysroot ${CMAKE_OSX_SYSROOT} -D__BLST_PORTABLE__"
                    bash ${blst_SOURCE_DIR}/build.sh
                COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
                WORKING_DIRECTORY ${blst_SOURCE_DIR}
                COMMENT "Building blst for iOS Device (arm64)"
                VERBATIM
            )
        endif()
    else()
        # Standard build for native platforms
        if(APPLE)
            # Get SDK paths only when building for macOS
            execute_process(
                COMMAND xcrun --sdk macosx --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(BLST_CFLAGS "-isysroot ${MACOS_SDK_PATH}")
        else()
            set(BLST_CFLAGS "${EXTRA_C_FLAGS}-ffunction-sections -fdata-sections")
        endif()

        add_custom_command(
            OUTPUT ${BLST_LIB}
            COMMAND ${CMAKE_COMMAND} -E env 
                CC=${CMAKE_C_COMPILER}
                CFLAGS=${BLST_CFLAGS}
                bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
            COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
            WORKING_DIRECTORY ${blst_SOURCE_DIR}
            COMMENT "Building blst for native architecture"
            VERBATIM
        )
    endif()
endif()

# Add a custom target that depends on the custom command
add_custom_target(blst_lib ALL DEPENDS ${BLST_LIB})

# Declare the blst library as an imported target
add_library(blst STATIC IMPORTED GLOBAL)
set_target_properties(blst PROPERTIES 
    IMPORTED_LOCATION ${BLST_LIB}
)

# Make sure the library is built before it's used
add_dependencies(blst blst_lib)
