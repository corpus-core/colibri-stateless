option(GENERATE_JAVA_SOURCES "Generate Java sources using SWIG" OFF)

if(GENERATE_JAVA_SOURCES)
    find_package(SWIG REQUIRED)
    include(UseSWIG)

    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Colibri.i PROPERTIES CPLUSPLUS OFF)
    set(CMAKE_SWIG_FLAGS "-module" "c4")
    
    find_package(Java REQUIRED)
    include(UseJava)
    
    if(NOT Java_INCLUDE_DIRS)
        if(APPLE)
            set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/darwin")
        elseif(UNIX)
            set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/linux")
        elseif(WIN32)
            set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/win32")
        endif()
        message(WARNING "Java include directories manually set to: ${Java_INCLUDE_DIRS}")
    endif()
    include_directories(${Java_INCLUDE_DIRS})
    include_directories(${CMAKE_SOURCE_DIR}/bindings)

    set(CMAKE_SWIG_FLAGS -package;com.corpuscore.colibri;-outdir;${CMAKE_CURRENT_BINARY_DIR}/java/com/corpuscore/colibri;-I${CMAKE_SOURCE_DIR}/bindings)
    swig_add_library(c4_java
        LANGUAGE java
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/java/com/corpuscore/colibri
        OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/java    
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/colibri.i
            ${CMAKE_CURRENT_SOURCE_DIR}/../colibri.c
    )
    target_link_libraries(c4_java proofer util verifier)
else()
    # Only build the native library without SWIG
    add_library(c4_java SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/../colibri.c
    )
    target_link_libraries(c4_java proofer util verifier)
endif()