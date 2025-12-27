find_package(CURL REQUIRED)

# Create a static library with all server logic (for testing)
# Shared async I/O utils
add_library(serverio STATIC
  uv_util.c
  configure.c
)
target_link_libraries(serverio PUBLIC util libuv llhttp CURL::libcurl )
target_include_directories(serverio INTERFACE .)
