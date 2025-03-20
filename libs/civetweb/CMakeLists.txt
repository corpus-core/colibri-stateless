# Civetweb via FetchContent herunterladen
include(FetchContent)

# Set policy CMP0135 to NEW to avoid download timestamp warning
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Store original compiler flags before modifying them
set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Disable sanitizers for civetweb
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-sanitize=all")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=all")

# Disable specific warnings that appear in civetweb
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "AppleClang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-atomic-implicit-seq-cst -Wno-alloca -Wno-cast-function-type-strict")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-cast-align -Wno-undef -Wno-date-time")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-atomic-implicit-seq-cst -Wno-alloca -Wno-cast-function-type-strict")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-align -Wno-undef -Wno-date-time")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-cast-align -Wno-undef -Wno-date-time")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-align -Wno-undef -Wno-date-time")
endif()

# Configure FetchContent with explicit options
FetchContent_Declare(
  civetweb_external
  GIT_REPOSITORY https://github.com/civetweb/civetweb.git
  GIT_TAG        v1.16
  GIT_SHALLOW    TRUE
)

# Override CMake variables BEFORE MakeAvailable
set(CIVETWEB_ENABLE_ZLIB OFF CACHE BOOL "Disable zlib compression" FORCE)
set(CIVETWEB_ENABLE_ASAN OFF CACHE BOOL "Disable ASAN" FORCE)
set(CIVETWEB_ENABLE_DEBUG_TOOLS OFF CACHE BOOL "Disable debug tools" FORCE)
set(CIVETWEB_ENABLE_CXX OFF CACHE BOOL "Disable C++ wrapper" FORCE)
set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "Disable testing" FORCE)
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "Disable server executable" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)

# IMPORTANT: Enable SSL and disable SSL dynamic loading
set(CIVETWEB_ENABLE_SSL ON CACHE BOOL "Enable SSL" FORCE)
set(CIVETWEB_ENABLE_SSL_DYNAMIC_LOADING OFF CACHE BOOL "Disable dynamic loading of SSL" FORCE)
set(CIVETWEB_SSL_OPENSSL_API_1_1 ON CACHE BOOL "Use OpenSSL 1.1 API" FORCE)

# Define NO_ZLIB to 1 to completely disable zlib in code
add_compile_definitions(NO_ZLIB=1)

# Fetch and build civetweb with modified compiler flags
FetchContent_MakeAvailable(civetweb_external)

# Restore original compiler flags for the rest of the project
set(CMAKE_C_FLAGS "${SAVED_CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${SAVED_CMAKE_CXX_FLAGS}")

# Create an alias so we can use "civetweb" instead of "civetweb-c-library"
add_library(civetweb ALIAS civetweb-c-library)

# Function to properly link civetweb with all needed dependencies
function(target_link_civetweb target)
  # First, link against civetweb library
  target_link_libraries(${target} PRIVATE civetweb-c-library)
  
  # Add the required include directories
  get_target_property(civetweb_source_dir civetweb-c-library SOURCE_DIR)
  target_include_directories(${target} PRIVATE ${civetweb_source_dir}/include)
  
  # Find and link OpenSSL - IMPORTANT for SSL support
  find_package(OpenSSL REQUIRED)
  target_link_libraries(${target} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  
  # Platform-specific libraries
  if(APPLE)
    # Mac requires these frameworks
    target_link_libraries(${target} PRIVATE "-framework Foundation" "-framework Security")
  endif()
  
  if(UNIX)
    # Linux and Mac both need pthread
    target_link_libraries(${target} PRIVATE pthread)
    
    # Linux-specific libraries
    if(NOT APPLE)
      target_link_libraries(${target} PRIVATE dl rt)
    endif()
  endif()
  
  if(WIN32)
    # Windows specific libraries
    target_link_libraries(${target} PRIVATE ws2_32)
  endif()
endfunction()

# Civetweb statisch verlinken und Includes hinzufügen
#target_link_libraries(server
#  PRIVATE 
#    your_library
#    $<TARGET_OBJECTS:civetweb-objects>  # Civetweb-Objekte direkt verlinken
#)

# Include-Pfade
#target_include_directories(server
#  PRIVATE 
#    ${civetweb_SOURCE_DIR}/include
#)