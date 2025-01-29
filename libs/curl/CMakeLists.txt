# Finde die libcurl-Bibliothek
find_package(CURL REQUIRED)

add_library(curl_fetch STATIC 
  http.c
)

# Verlinke libcurl mit deiner ausführbaren Datei
target_link_libraries(curl_fetch util CURL::libcurl)
add_dependencies(curl_fetch util)

target_include_directories(curl_fetch PRIVATE ../../src/util)
