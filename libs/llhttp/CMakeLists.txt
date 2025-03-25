cmake_minimum_required(VERSION 3.11)

include(FetchContent)

# Set policy CMP0135 to NEW if CMake version >= 3.24
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

# Use the tarball URL as recommended instead of git repo
FetchContent_Declare(llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Set cache variables for static library build
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")

FetchContent_MakeAvailable(llhttp)

# Create an INTERFACE library with include directories
add_library(llhttp INTERFACE)
target_link_libraries(llhttp INTERFACE llhttp_static)
target_include_directories(llhttp INTERFACE ${llhttp_SOURCE_DIR}/include)

# For backward compatibility
set(LLHTTP_INCLUDE_DIR ${llhttp_SOURCE_DIR}/include PARENT_SCOPE)
set(LLHTTP_LIBRARY llhttp_static PARENT_SCOPE) 