# Finde alle C-Dateien im aktuellen Verzeichnis
file(GLOB TEST_SOURCES *.c)

foreach(test_source ${TEST_SOURCES})
    # Extrahiere den Dateinamen ohne Pfad und Suffix
    get_filename_component(test_name ${test_source} NAME_WE)

    # Erstelle eine ausführbare Datei für den Test
    add_executable(${test_name} ${test_source})

    # Füge die Include-Verzeichnisse hinzu
    target_include_directories(${test_name} PRIVATE ${unity_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src)

    # Verknüpfe die Testausführbare mit Unity und deinem Code
    target_link_libraries(${test_name} unity verifier proofer)

    # Füge den Test zu CTest hinzu
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()