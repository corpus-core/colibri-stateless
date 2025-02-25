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
elseif(WIN32)
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${CMAKE_COMMAND} -E env CFLAGS=${EXTRA_C_FLAGS}-ffunction-sections\ -fdata-sections ${blst_SOURCE_DIR}/build.bat -D__BLST_PORTABLE__
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
    elseif(CMAKE_CROSSCOMPILING)
        add_custom_command(
            OUTPUT ${BLST_LIB}
            COMMAND ${CMAKE_COMMAND} -E env 
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                CFLAGS=${EXTRA_C_FLAGS}-ffunction-sections\ -fdata-sections
                bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
            COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
            WORKING_DIRECTORY ${blst_SOURCE_DIR}
            COMMENT "Building and moving libblst.a with cross-compilation toolchain"
            VERBATIM
        )
    else()
        # Standard build for native platforms
        if(APPLE)
            # Get SDK paths only when building for iOS
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
                "CC=${CMAKE_C_COMPILER}"
                "CFLAGS=${BLST_CFLAGS}"
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

# iOS-specific build for Swift
if(SWIFT)
    # Get iOS SDK paths
    execute_process(
        COMMAND xcrun --sdk iphoneos --show-sdk-path
        OUTPUT_VARIABLE IOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND xcrun --sdk iphonesimulator --show-sdk-path
        OUTPUT_VARIABLE IOS_SIMULATOR_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Für iOS bauen wir beide Architekturen und erstellen dann eine Fat Binary
    set(BLST_IOS_LIB ${BLST_LIB_OUTPUT_DIR}/libblst_ios.a)
    set(BLST_LIB_ARM64 ${BLST_LIB_OUTPUT_DIR}/libblst_arm64.a)
    set(BLST_LIB_X86_64 ${BLST_LIB_OUTPUT_DIR}/libblst_x86_64.a)

    # Build für ARM64 (iOS Devices)
    add_custom_command(
        OUTPUT ${BLST_LIB_ARM64}
        COMMAND ${CMAKE_COMMAND} -E env 
            "CC=${CMAKE_C_COMPILER}"
            "CFLAGS=-target arm64-apple-ios -isysroot ${IOS_SDK_PATH}"
            bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
        COMMAND ${CMAKE_COMMAND} -E copy ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB_ARM64}
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building blst for iOS ARM64"
        VERBATIM
    )

    # Build für x86_64 (iOS Simulator)
    add_custom_command(
        OUTPUT ${BLST_LIB_X86_64}
        COMMAND ${CMAKE_COMMAND} -E env 
            "CC=${CMAKE_C_COMPILER}"
            "CFLAGS=-target x86_64-apple-ios-simulator -isysroot ${IOS_SIMULATOR_SDK_PATH}"
            bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
        COMMAND ${CMAKE_COMMAND} -E copy ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB_X86_64}
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building blst for iOS Simulator x86_64"
        VERBATIM
    )

    # Kombiniere zu iOS Fat Binary
    add_custom_command(
        OUTPUT ${BLST_IOS_LIB}
        DEPENDS ${BLST_LIB_ARM64} ${BLST_LIB_X86_64}
        COMMAND lipo -create 
            ${BLST_LIB_ARM64}
            ${BLST_LIB_X86_64}
            -output ${BLST_IOS_LIB}
        COMMAND touch ${BLST_IOS_LIB}.fat
        COMMENT "Creating Fat Binary for iOS"
        VERBATIM
    )

    # Custom target für die iOS Fat Binary
    add_custom_target(blst_ios ALL 
        DEPENDS ${BLST_IOS_LIB}
        SOURCES ${blst_SOURCE_DIR}/src/server.c
    )
endif()
