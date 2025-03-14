cmake_minimum_required(VERSION 3.11)

include(FetchContent)

# Fetch LibTomMath from GitHub
FetchContent_Declare(
    libtommath
    GIT_REPOSITORY https://github.com/libtom/libtommath.git
    GIT_TAG v1.3.0  # Use the latest stable version
)

# Make the content available
FetchContent_MakeAvailable(libtommath)

# Ensure it's built as a static library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# The correct target name is 'libtommath', not 'tommath'
if(NOT TARGET libtommath)
    message(FATAL_ERROR "LibTomMath target 'libtommath' not found")
endif()

# Create an alias 'tommath' that points to 'libtommath'
# This makes it easier to use throughout your project
add_library(tommath ALIAS libtommath) 