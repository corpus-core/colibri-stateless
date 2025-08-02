# Python bindings for Colibri
# This CMakeLists.txt is included as a subdirectory from the main project

# Find required packages
find_package(Python COMPONENTS Interpreter Development REQUIRED)

# Find pybind11 using the Python interpreter
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(pybind11 REQUIRED PATHS ${pybind11_DIR})

# Create the pybind11 module with both C++ and C sources
pybind11_add_module(_native 
    src/bindings.cpp
    ${CMAKE_SOURCE_DIR}/bindings/colibri.c
)

# Include directories for bindings (only generic APIs, no chain-specific headers)
target_include_directories(_native PRIVATE
    ${CMAKE_SOURCE_DIR}/bindings
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/proofer
    ${CMAKE_SOURCE_DIR}/src/verifier
)

# Link main functionality libraries into the Python module
# CMake will automatically resolve all dependencies (eth_verifier, eth_proofer, crypto, etc.)
target_link_libraries(_native PRIVATE
    verifier
    proofer
)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(_native PRIVATE m dl pthread)
elseif(APPLE)
    target_link_libraries(_native PRIVATE m pthread)
elseif(WIN32)
    target_link_libraries(_native PRIVATE ws2_32)
endif()

# Compiler-specific options
target_compile_definitions(_native PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})

# Set the output name to match the expected import name
set_target_properties(_native PROPERTIES
    OUTPUT_NAME "_native"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
)

# Enable all warnings but don't treat them as errors
if(MSVC)
    target_compile_options(_native PRIVATE /W4)
else()
    target_compile_options(_native PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Disable specific warnings that may come from the C codebase
if(NOT MSVC)
    target_compile_options(_native PRIVATE 
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-sign-compare
        -Wno-unused-function
    )
endif()

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(_native PRIVATE -O3)
    if(NOT WIN32)
        target_compile_options(_native PRIVATE -flto)
        target_link_options(_native PRIVATE -flto)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Enhanced debug build with full symbol information
    target_compile_options(_native PRIVATE -g3 -O0 -fno-omit-frame-pointer)
    target_compile_definitions(_native PRIVATE DEBUG _DEBUG)
    if(APPLE)
        target_compile_options(_native PRIVATE -gdwarf-4)
    endif()
    # Disable optimizations that could interfere with debugging
    target_compile_options(_native PRIVATE -fno-inline -fno-inline-functions)
    message(STATUS "Debug build configured with full debugging symbols")
endif()

# Custom target to copy the built module to the Python package directory
add_custom_command(TARGET _native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_native> ${CMAKE_CURRENT_SOURCE_DIR}/src/colibri/
    COMMENT "Copying Python extension module to package directory"
)

# Add dependencies to ensure proper build order
add_dependencies(_native verifier proofer)

# Debug info
message(STATUS "Python bindings configured:")
message(STATUS "  Main libraries: verifier, proofer (with auto-resolved dependencies)")
message(STATUS "  Python: ${Python_EXECUTABLE}")
message(STATUS "  pybind11: ${pybind11_VERSION}")