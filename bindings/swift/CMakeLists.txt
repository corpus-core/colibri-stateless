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
    # Create XCFramework from separate architecture builds
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/c4_swift.xcframework
        # Create base directories
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib
        
        # Copy simulator (x86_64) libraries individually
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/libs/crypto/libcrypto.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libcrypto.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/libs/blst/libblst.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libblst.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/src/util/libutil.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libutil.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/src/proofer/libproofer.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libproofer.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/src/verifier/libverifier.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libverifier.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${SWIFT_X86_BUILD}/bindings/swift/libc4_swift_binding.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libc4_swift_binding.a
            
        # Copy device (arm64) libraries individually
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/libs/crypto/libcrypto.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libcrypto.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/libs/blst/libblst.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libblst.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/src/util/libutil.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libutil.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/src/proofer/libproofer.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libproofer.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/src/verifier/libverifier.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libverifier.a
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/bindings/swift/libc4_swift_binding.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libc4_swift_binding.a
        
        # Create combined static libraries
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/c4_swift
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libcrypto.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libblst.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libutil.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libproofer.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libverifier.a
            ${CMAKE_BINARY_DIR}/framework/ios-simulator/c4_swift.framework/lib/libc4_swift_binding.a
            
        COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/c4_swift
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libcrypto.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libblst.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libutil.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libproofer.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libverifier.a
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/lib/libc4_swift_binding.a
        
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
            -c "Add :MinimumOSVersion string 12.0"
            -c "Add :CFBundleExecutable string c4_swift"
            ${CMAKE_BINARY_DIR}/framework/ios-device/c4_swift.framework/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" 
            -c "Add :CFBundleIdentifier string com.c4.swift"
            -c "Add :CFBundleName string c4_swift"
            -c "Add :CFBundleVersion string 1.0"
            -c "Add :CFBundleShortVersionString string 1.0"
            -c "Add :MinimumOSVersion string 12.0"
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


