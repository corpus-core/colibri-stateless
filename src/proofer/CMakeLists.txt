add_library(proofer STATIC 
  proofer.c
)
target_include_directories(proofer PRIVATE util)
target_link_libraries(proofer PRIVATE     util)
