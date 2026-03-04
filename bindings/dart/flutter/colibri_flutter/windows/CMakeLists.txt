cmake_minimum_required(VERSION 3.14)
project(colibri_flutter LANGUAGES CXX)

set(PLUGIN_NAME "colibri_flutter_plugin")

add_library(${PLUGIN_NAME} SHARED
  "colibri_flutter_plugin.cpp"
)
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Bundle colibri.dll so the app can load it via Dart FFI.
set(COLIBRI_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/colibri.dll")
if(EXISTS "${COLIBRI_DLL}")
  set(colibri_flutter_bundled_libraries "${COLIBRI_DLL}" PARENT_SCOPE)
endif()
