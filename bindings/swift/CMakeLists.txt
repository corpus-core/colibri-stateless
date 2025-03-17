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
    
    # Generate copy commands for simulator libraries
    set(SIMULATOR_COPY_COMMANDS "")
    foreach(lib IN LISTS C4_LIBRARIES)
        get_filename_component(lib_name ${lib} NAME)
        set(SIMULATOR_COPY_COMMANDS "${SIMULATOR_COPY_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E copy ${SWIFT_X86_BUILD}/${lib} ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/${lib_name}")
    endforeach()
    
    # Generate copy commands for device libraries
    set(DEVICE_COPY_COMMANDS "")
    foreach(lib IN LISTS C4_LIBRARIES)
        get_filename_component(lib_name ${lib} NAME)
        set(DEVICE_COPY_COMMANDS "${DEVICE_COPY_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${lib} ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/${lib_name}")
    endforeach()
    
    # Generate libtool arguments
    set(SIMULATOR_LIBTOOL_ARGS "")
    set(DEVICE_LIBTOOL_ARGS "")
    foreach(lib IN LISTS C4_LIBRARIES)
        get_filename_component(lib_name ${lib} NAME)
        set(SIMULATOR_LIBTOOL_ARGS "${SIMULATOR_LIBTOOL_ARGS} ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/${lib_name}")
        set(DEVICE_LIBTOOL_ARGS "${DEVICE_LIBTOOL_ARGS} ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/${lib_name}")
    endforeach()

    # Create XCFramework from separate architecture builds
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        # Create base directories
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib
        
        # Copy simulator (x86_64) libraries
        ${SIMULATOR_COPY_COMMANDS}
            
        # Copy device (arm64) libraries
        ${DEVICE_COPY_COMMANDS}
        
        # Create combined static libraries
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/c4_swift ${SIMULATOR_LIBTOOL_ARGS}
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/c4_swift ${DEVICE_LIBTOOL_ARGS}
        
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
        
        COMMENT "Creating XCFramework from separate architecture builds"
        VERBATIM
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
        VERBATIM
    )

    add_custom_target(swift_package ALL
        DEPENDS ${CMAKE_BINARY_DIR}/swift_build.timestamp
    )
endif()


