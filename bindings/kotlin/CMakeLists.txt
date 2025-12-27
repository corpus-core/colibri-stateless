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

# Option to generate Java sources (and wrapper) with SWIG
option(GENERATE_JAVA_SOURCES "Generate Java sources using SWIG" OFF)

# Always find SWIG and Java, as we need the wrapper even when not generating sources
find_package(SWIG REQUIRED)
include(UseSWIG)

find_package(Java REQUIRED)
include(UseJava)

# Ensure RPATH is handled for macOS builds of this specific library
if(APPLE)
   set(CMAKE_MACOSX_RPATH TRUE)
endif()

# Set Java include directories
if(ANDROID)
    # Use Android NDK's JNI headers
    set(Java_INCLUDE_DIRS 
        "${ANDROID_NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/sysroot/usr/include"
        "${ANDROID_NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/sysroot/usr/include/linux")
elseif(NOT Java_INCLUDE_DIRS)
    if(APPLE)
        set(Java_INCLUDE_DIRS 
            "$ENV{JAVA_HOME}/include" 
            "$ENV{JAVA_HOME}/include/darwin")
    elseif(UNIX)
        set(Java_INCLUDE_DIRS 
            "$ENV{JAVA_HOME}/include" 
            "$ENV{JAVA_HOME}/include/linux")
    elseif(WIN32)
        set(Java_INCLUDE_DIRS 
            "$ENV{JAVA_HOME}/include" 
            "$ENV{JAVA_HOME}/include/win32")
    endif()
    message(WARNING "Java include directories manually set to: ${Java_INCLUDE_DIRS}")
endif()
include_directories(${Java_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/bindings)
include_directories(${CMAKE_SOURCE_DIR}/bindings)

# Define SWIG flags (used in both cases)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Colibri.i PROPERTIES CPLUSPLUS OFF)
set(CMAKE_SWIG_FLAGS "-module" "c4" "-package" "com.corpuscore.colibri" "-I${CMAKE_SOURCE_DIR}/bindings")

# Define sources
set(SWIG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/colibri.i
    ${CMAKE_CURRENT_SOURCE_DIR}/../colibri.c
    ${CMAKE_CURRENT_SOURCE_DIR}/jni_bridge.c
)

if(GENERATE_JAVA_SOURCES)
    # Generate Java sources and wrapper
    swig_add_library(c4_java
        LANGUAGE java
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/java/com/corpuscore/colibri
        OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/java    
        SOURCES ${SWIG_SOURCES}
    )
else()
    # Use pre-generated wrapper, still build the library with JNI
    swig_add_library(c4_java
        LANGUAGE java
        SOURCES ${SWIG_SOURCES}
    )
endif()

# Link libraries in both cases
target_link_libraries(c4_java prover util verifier eth_verifier)