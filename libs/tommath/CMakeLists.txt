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

# Make sure the include directory is properly exposed
# (This helps IDEs find the headers)
if(TARGET tommath)
    set_target_properties(tommath PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${libtommath_SOURCE_DIR}")
endif()

# Export the tommath target so it can be used with target_link_libraries
# in other parts of your project
if(NOT TARGET tommath)
    message(FATAL_ERROR "LibTomMath target 'tommath' not found")
endif() 