add_library(proofer STATIC 
  proofer.c
)
target_include_directories(proofer INTERFACE .)
target_link_libraries(proofer PUBLIC util)
