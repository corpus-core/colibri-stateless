find_package(SWIG REQUIRED)
include(${SWIG_USE_FILE})

if(BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    set(CMAKE_SWIG_FLAGS "")
    set(SWIG_PYTHON_OUTPUT_DIR "${OUTPUT_DIRECTORY}/python")
endif()

if(BUILD_NODEJS)
    find_package(Node REQUIRED)
    set(CMAKE_SWIG_FLAGS "-node")
endif()

if(BUILD_EMSCRIPTEN)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "Emscripten bindings require Emscripten. Please compile with emcmake cmake.")
    endif()
    set(CMAKE_SWIG_FLAGS "-javascript")
endif()





if(BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    set(CMAKE_SWIG_FLAGS "")
    set(SWIG_PYTHON_OUTPUT_DIR "${OUTPUT_DIRECTORY}/python")

    set_source_files_properties(python/c4.i PROPERTIES CPLUSPLUS OFF)
    
    swig_add_library(c4_python
        LANGUAGE python
        SOURCES python/c4.i
    )
    
    target_include_directories(c4_python PRIVATE
        ${Python_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src
    )
    
    target_link_libraries(c4_python PRIVATE
        proofer
        util
        verifier
        ${Python_LIBRARIES}
    )
    
    set_target_properties(c4_python PROPERTIES
        OUTPUT_NAME "_c4"
        LIBRARY_OUTPUT_DIRECTORY ${SWIG_PYTHON_OUTPUT_DIR}
    )
endif()

if(BUILD_NODEJS)
    find_package(Node REQUIRED)
    set(CMAKE_SWIG_FLAGS "-node")

    set_source_files_properties(javascript/c4.i PROPERTIES CPLUSPLUS OFF)
    
    swig_add_library(c4_js
        LANGUAGE javascript
        SOURCES javascript/c4.i
    )
    
    target_include_directories(c4_js PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    
    target_link_libraries(c4_js PRIVATE
        c4Lib
    )
    
    # Emscripten-spezifische Flags
    if(EMSCRIPTEN)
        set_target_properties(c4_js PROPERTIES
            LINK_FLAGS "-s WASM=1 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -s EXPORT_ES6=1"
            SUFFIX ".mjs"
        )
    endif()
endif() 