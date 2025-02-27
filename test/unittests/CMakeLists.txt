# Finde alle C-Dateien im aktuellen Verzeichnis
file(GLOB TEST_SOURCES test_*.c)

#add_definitions(-DTEST_DATA_DIR=${CMAKE_SOURCE_DIR}/test/data)

foreach(test_source ${TEST_SOURCES})
    # Extrahiere den Dateinamen ohne Pfad und Suffix
    get_filename_component(test_name ${test_source} NAME_WE)

    # Erstelle eine ausführbare Datei für den Test
    add_executable(${test_name} ${test_source})

    # Füge die Include-Verzeichnisse hinzu
    target_include_directories(${test_name} PRIVATE ${unity_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src)

    # Verknüpfe die Testausführbare mit Unity und deinem Code
    target_link_libraries(${test_name} unity verifier proofer)

#    target_compile_definitions(${test_name} PRIVATE TESTDATA_DIR="${CMAKE_SOURCE_DIR}/test/data")

    # Füge den Test zu CTest hinzu
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()


if(MSVC)
    # Add Windows-specific definitions for test compatibility
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    # Define setenv macro for Windows compatibility, ignoring the overwrite parameter
    add_definitions(-Dsetenv(name,value,overwrite)=_putenv_s(name,value))
endif()



# Erstelle eine ausführbare Datei für den Test
add_executable(verify_only verify_only.c)

# Füge die Include-Verzeichnisse hinzu
target_include_directories(verify_only PRIVATE ${unity_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src)

# Verknüpfe die Testausführbare mit Unity und deinem Code
target_link_libraries(verify_only verifier )
