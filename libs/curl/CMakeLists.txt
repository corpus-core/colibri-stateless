# Finde die libcurl-Bibliothek
find_package(CURL REQUIRED)

# Verlinke libcurl mit deiner ausführbaren Datei
target_link_libraries(my_executable CURL::libcurl)

add_library(util STATIC 
  bytes.c 
  ssz.c
  crypto.c
  plugin.c
)
target_include_directories(util PRIVATE ../../libs/crypto)
target_link_libraries(util PRIVATE crypto blst)