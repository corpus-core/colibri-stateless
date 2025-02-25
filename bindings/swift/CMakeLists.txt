include_directories(${CMAKE_SOURCE_DIR}/bindings)

# Funktion zum Erstellen einer Fat Binary aus einer existierenden Bibliothek
function(ensure_fat_binary lib_path)
    get_filename_component(lib_dir ${lib_path} DIRECTORY)
    get_filename_component(lib_name ${lib_path} NAME_WE)
    get_filename_component(lib_ext ${lib_path} EXT)

    set(x86_64_lib "${lib_dir}/${lib_name}_x86_64${lib_ext}")
    set(arm64_lib "${lib_dir}/${lib_name}_arm64${lib_ext}")
    set(x86_64_obj "${lib_dir}/${lib_name}_x86_64.o")

    add_custom_command(
        OUTPUT "${lib_path}.fat"
        COMMAND ${CMAKE_COMMAND} -E copy "${lib_path}" "${arm64_lib}"
        # Compile x86_64 version
        COMMAND ${CMAKE_C_COMPILER} 
            -target x86_64-apple-ios-simulator
            -isysroot ${IOS_SIMULATOR_SDK_PATH}
            -c ${CMAKE_CURRENT_SOURCE_DIR}/../colibri.c
            -o "${x86_64_obj}"
        COMMAND ${CMAKE_AR} cr "${x86_64_lib}" "${x86_64_obj}"
        # Create fat binary
        COMMAND lipo -create 
            "${arm64_lib}"
            "${x86_64_lib}"
            -output "${lib_path}"
        COMMAND ${CMAKE_COMMAND} -E touch "${lib_path}.fat"
        DEPENDS "${lib_path}"
        COMMENT "Ensuring ${lib_path} is a fat binary"
        VERBATIM
    )
endfunction()

# Erstelle die iOS-Bibliothek
add_library(c4_swift_binding STATIC 
    ../colibri.c
)

add_dependencies(c4_swift_binding util)
target_include_directories(c4_swift_binding PRIVATE ../../src/util)
target_link_libraries(c4_swift_binding proofer util verifier)

# Setze iOS-spezifische Eigenschaften für das binding
set_ios_target_properties(c4_swift_binding)

# Get iOS SDK paths
execute_process(
    COMMAND xcrun --sdk iphonesimulator --show-sdk-path
    OUTPUT_VARIABLE IOS_SIMULATOR_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Stelle sicher, dass alle benötigten Bibliotheken Fat Binaries sind
ensure_fat_binary("${CMAKE_BINARY_DIR}/libs/crypto/libcrypto.a")
ensure_fat_binary("${CMAKE_BINARY_DIR}/libs/blst/libblst_ios.a")
ensure_fat_binary("${CMAKE_BINARY_DIR}/src/util/libutil.a")
ensure_fat_binary("${CMAKE_BINARY_DIR}/src/proofer/libproofer.a")
ensure_fat_binary("${CMAKE_BINARY_DIR}/src/verifier/libverifier.a")
ensure_fat_binary("${CMAKE_BINARY_DIR}/bindings/swift/libc4_swift_binding.a")

# Kombiniere alle Bibliotheken zu einer finalen Fat Binary
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/libc4_swift.a
    COMMAND libtool -static -o ${CMAKE_BINARY_DIR}/libc4_swift.a
            ${CMAKE_BINARY_DIR}/libs/crypto/libcrypto.a
            ${CMAKE_BINARY_DIR}/libs/blst/libblst_ios.a
            ${CMAKE_BINARY_DIR}/src/util/libutil.a
            ${CMAKE_BINARY_DIR}/src/proofer/libproofer.a
            ${CMAKE_BINARY_DIR}/src/verifier/libverifier.a
            ${CMAKE_BINARY_DIR}/bindings/swift/libc4_swift_binding.a
    COMMAND lipo -info ${CMAKE_BINARY_DIR}/libc4_swift.a
    DEPENDS 
        "${CMAKE_BINARY_DIR}/libs/crypto/libcrypto.a.fat"
        "${CMAKE_BINARY_DIR}/libs/blst/libblst_ios.a.fat"
        "${CMAKE_BINARY_DIR}/src/util/libutil.a.fat"
        "${CMAKE_BINARY_DIR}/src/proofer/libproofer.a.fat"
        "${CMAKE_BINARY_DIR}/src/verifier/libverifier.a.fat"
        "${CMAKE_BINARY_DIR}/bindings/swift/libc4_swift_binding.a.fat"
        blst_ios
    COMMENT "Combining static libs into fat binary"
)

# Benutzerdefiniertes Ziel, um die kombinierte Bibliothek zu bauen
add_custom_target(c4_swift ALL
    DEPENDS ${CMAKE_BINARY_DIR}/libc4_swift.a
)