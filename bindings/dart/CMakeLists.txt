include_directories(${CMAKE_SOURCE_DIR}/bindings)
include_directories(${CMAKE_SOURCE_DIR}/src/util)
include_directories(${CMAKE_SOURCE_DIR}/src/prover)
include_directories(${CMAKE_SOURCE_DIR}/src/verifier)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/dart/native)

add_library(colibri_dart SHARED
    ../colibri.c
)

set_target_properties(colibri_dart PROPERTIES
    OUTPUT_NAME "colibri"
)

if(WIN32)
    set_target_properties(colibri_dart PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_link_libraries(colibri_dart
    PRIVATE
    util
    prover
    verifier
    eth_verifier
)

add_custom_command(TARGET colibri_dart POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:colibri_dart> ${CMAKE_SOURCE_DIR}/bindings/dart/native/
    COMMENT "Copying Dart native library to bindings/dart/native"
)
