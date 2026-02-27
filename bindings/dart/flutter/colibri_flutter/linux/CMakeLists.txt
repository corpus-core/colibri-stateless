cmake_minimum_required(VERSION 3.13)
project(colibri_flutter LANGUAGES CXX)

set(PLUGIN_NAME "colibri_flutter")

add_library(${PLUGIN_NAME}_plugin SHARED
  "colibri_flutter_plugin.cc"
)
target_link_libraries(${PLUGIN_NAME}_plugin PRIVATE flutter)
target_include_directories(${PLUGIN_NAME}_plugin PRIVATE "${CMAKE_SOURCE_DIR}")

# Bundle libcolibri.so so the app can load it via Dart FFI (e.g. from lib/ next to executable).
set(COLIBRI_SO "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcolibri.so")
if(EXISTS "${COLIBRI_SO}")
  set(${PLUGIN_NAME}_bundled_libraries "${COLIBRI_SO}" PARENT_SCOPE)
endif()
