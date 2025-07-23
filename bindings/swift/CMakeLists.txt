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

# Create the C binding library
add_library(c4_swift_binding STATIC 
    ../colibri.c
)

add_dependencies(c4_swift_binding util proofer verifier)
target_include_directories(c4_swift_binding 
    PRIVATE 
    ../../src/util
    ../../src/proofer
    ../../src/verifier
)
target_link_libraries(c4_swift_binding
    PRIVATE
    util
    proofer
    verifier
)

# Only create XCFramework if we have both architectures
if(DEFINED SWIFT_X86_BUILD)
    if (EVMONE)
        set(EVMONE_LIBRARIES
            "libs/intx/libintx_wrapper.a"
            "src/chains/eth/precompiles/libeth_precompiles.a"
            "libs/evmone/libevmone_wrapper.a"
            "_deps/evmone_external-build/libevmone.a"
            "_deps/ethhash_external-build/libkeccak.a"        
        )
    endif()

    # Define the list of libraries with their paths
    set(C4_LIBRARIES
        "libs/crypto/libcrypto.a"
        "libs/blst/libblst.a"
        "src/util/libutil.a"
        "src/proofer/libproofer.a"
        ${EVMONE_LIBRARIES}
        "src/chains/eth/libeth_verifier.a"
        "src/chains/eth/libeth_proofer.a"
        "src/verifier/libverifier.a"
        "bindings/swift/libc4_swift_binding.a"
    )
    
    # Create response files for libtool
    set(SIMULATOR_LIBS_FILE "${CMAKE_BINARY_DIR}/simulator_libs.txt")
    set(DEVICE_LIBS_FILE "${CMAKE_BINARY_DIR}/device_libs.txt")
    
    file(WRITE ${SIMULATOR_LIBS_FILE} "")
    file(WRITE ${DEVICE_LIBS_FILE} "")
    
    foreach(lib IN LISTS C4_LIBRARIES)
        get_filename_component(lib_name ${lib} NAME)
        file(APPEND ${SIMULATOR_LIBS_FILE} "${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/${lib_name}\n")
        file(APPEND ${DEVICE_LIBS_FILE} "${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/${lib_name}\n")
    endforeach()
    
    # Initial setup command - Uses VERBATIM
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        # Create base directories
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib
        COMMENT "Starting XCFramework build"
        VERBATIM
    )
    
    # Add library copy commands
    foreach(lib IN LISTS C4_LIBRARIES)
        get_filename_component(lib_name ${lib} NAME)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/c4_swift.xcframework
            COMMAND ${CMAKE_COMMAND} -E copy ${SWIFT_X86_BUILD}/${lib} ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/${lib_name}
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${lib} ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/${lib_name}
            APPEND
        )
    endforeach()
    
    # Add remaining commands
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        # Create combined static libraries using response files
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/c4_swift @${SIMULATOR_LIBS_FILE}
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/c4_swift @${DEVICE_LIBS_FILE}
        
        # Copy headers
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/Headers
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/Headers
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bindings/colibri.h ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/Headers/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bindings/colibri.h ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/Headers/
        
        # Create Info.plist files
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" 
            -c "Add :CFBundleIdentifier string com.c4.swift"
            -c "Add :CFBundleName string c4_swift"
            -c "Add :CFBundleVersion string 1.0"
            -c "Add :CFBundleShortVersionString string 1.0"
            -c "Add :MinimumOSVersion string 13.0"
            -c "Add :CFBundleExecutable string c4_swift"
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" 
            -c "Add :CFBundleIdentifier string com.c4.swift"
            -c "Add :CFBundleName string c4_swift"
            -c "Add :CFBundleVersion string 1.0"
            -c "Add :CFBundleShortVersionString string 1.0"
            -c "Add :MinimumOSVersion string 13.0"
            -c "Add :CFBundleExecutable string c4_swift"
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/Info.plist
        
        # Create XCFramework
        COMMAND xcodebuild -create-xcframework
            -framework ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework
            -framework ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework
            -output ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        
        APPEND
        COMMENT "Finalizing XCFramework"
    )

    # Add custom targets
    add_custom_target(c4_swift ALL
        DEPENDS ${CMAKE_BINARY_DIR}/c4_swift.xcframework
    )

    # Add Swift package build command
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/swift_build.timestamp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/include
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bindings/colibri.h ${CMAKE_CURRENT_SOURCE_DIR}/src/include/
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/swift_build.timestamp
        DEPENDS ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        COMMENT "Setting up Swift package"
    )

    add_custom_target(swift_package ALL
        DEPENDS ${CMAKE_BINARY_DIR}/swift_build.timestamp
    )
endif()


