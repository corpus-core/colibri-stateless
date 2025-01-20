# Registriere die bereits kompilierte statische Bibliothek
add_library(blst STATIC IMPORTED)


set_target_properties(blst PROPERTIES
IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bin/libblst.a"
INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)