cmake_minimum_required(VERSION 3.10)

project(evmone-wrapper)

# Create a simple standalone library with just the C wrapper
add_library(evmone_wrapper STATIC evmone_c_wrapper.cpp)

# Set include directories
target_include_directories(evmone_wrapper PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set C++11 standard
set_target_properties(evmone_wrapper PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED TRUE
  CXX_EXTENSIONS FALSE
)

# Create an interface library for dependency tracking
add_library(evmone INTERFACE)
set_target_properties(evmone PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Install the library and header
install(TARGETS evmone_wrapper
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(FILES evmone_c_wrapper.h DESTINATION include)