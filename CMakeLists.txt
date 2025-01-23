cmake_minimum_required(VERSION 3.5.1)
project(c4)


set(OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)


if(CMAKE_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")

    if(POLICY CMP0069)
        cmake_policy(SET CMP0069 NEW)
    endif()
    # Aktivieren von Link-Time Optimization
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    # Compiler-Flags für Größe-Optimierung
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

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


add_subdirectory(src)
add_subdirectory(libs)

# Define a custom target named 'doc'
add_custom_target(doc ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Generating documentation..."
    COMMAND ${CMAKE_COMMAND} -E env node ${CMAKE_SOURCE_DIR}/scripts/update_doc.js
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts
    COMMENT "Running update_doc.js to generate documentation"
)

# Erstelle die Shared Library und verlinke die statischen Bibliotheken
add_library(c4Lib SHARED src/util/version.c)
target_link_libraries(c4Lib PRIVATE crypto verifier util blst)
set_target_properties(c4Lib PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
    VERSION 1.0
    SOVERSION 1
)