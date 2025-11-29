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

include_directories(${CMAKE_SOURCE_DIR}/bindings)

# Auto-detect iOS SDK paths if building for iOS and not set
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_SYSROOT)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        # Use iOS Simulator SDK for x86_64
        execute_process(
            COMMAND xcrun --sdk iphonesimulator --show-sdk-path
            OUTPUT_VARIABLE IOS_SIMULATOR_SDK_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE SDK_RESULT
        )
        if(SDK_RESULT EQUAL 0)
            set(CMAKE_OSX_SYSROOT ${IOS_SIMULATOR_SDK_PATH})
            message(STATUS "Auto-detected iOS Simulator SDK: ${CMAKE_OSX_SYSROOT}")
        else()
            message(FATAL_ERROR "iOS Simulator SDK not found. Install Xcode.")
        endif()
    else()
        # Use iOS Device SDK for arm64
        execute_process(
            COMMAND xcrun --sdk iphoneos --show-sdk-path
            OUTPUT_VARIABLE IOS_DEVICE_SDK_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE SDK_RESULT
        )
        if(SDK_RESULT EQUAL 0)
            set(CMAKE_OSX_SYSROOT ${IOS_DEVICE_SDK_PATH})
            message(STATUS "Auto-detected iOS Device SDK: ${CMAKE_OSX_SYSROOT}")
        else()
            message(FATAL_ERROR "iOS Device SDK not found. Install Xcode.")
        endif()
    endif()
endif()

# Create the C binding library
add_library(c4_swift_binding STATIC 
    ../colibri.c
)

# Add build dependencies based on enabled features
set(C4_SWIFT_BUILD_DEPS util prover verifier)

# Add EVMOne build dependencies if enabled
if(EVMONE)
    list(APPEND C4_SWIFT_BUILD_DEPS evmone_wrapper)
endif()

add_dependencies(c4_swift_binding ${C4_SWIFT_BUILD_DEPS})
target_include_directories(c4_swift_binding 
    PRIVATE 
    ../../src/util
    ../../src/prover
    ../../src/verifier
)
# Link dependencies based on enabled features
set(C4_SWIFT_DEPENDENCIES util prover verifier eth_verifier)

# Add EVMOne dependencies if enabled
if(EVMONE)
    list(APPEND C4_SWIFT_DEPENDENCIES evmone_wrapper)
endif()

target_link_libraries(c4_swift_binding
    PRIVATE
    ${C4_SWIFT_DEPENDENCIES}
)


