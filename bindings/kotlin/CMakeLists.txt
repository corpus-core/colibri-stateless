# SWIG muss installiert und in deinem PATH sein.
find_package(SWIG REQUIRED)
include(UseSWIG)

# Wir verwenden reines C (kein C++), deshalb schalten wir C++ aus.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/proofer.i PROPERTIES CPLUSPLUS OFF)

# Setze SWIG-Flags: -module gibt den Modulnamen an.
set(CMAKE_SWIG_FLAGS "-module" "colibri")

message(STATUS "JAVA_HOME: $ENV{JAVA_HOME}")
# Optional: Falls du Java-Unterstützung für SWIG benötigst:
find_package(Java REQUIRED)
include(UseJava)
if(NOT Java_INCLUDE_DIRS)
    if(APPLE)
        # macOS: JNI-Header liegen unter include und include/darwin
        set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/darwin")
    elseif(UNIX)
        # Linux: JNI-Header liegen unter include und include/linux
        set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/linux")
    elseif(WIN32)
        # Windows: typischerweise include und include/win32
        set(Java_INCLUDE_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/win32")
    endif()
    message(WARNING "Java include directories manually set to: ${Java_INCLUDE_DIRS}")
endif()
include_directories(${Java_INCLUDE_DIRS})
message(STATUS "Java include directories: ${Java_INCLUDE_DIRS}")

# Stelle sicher, dass der Pfad zu deinen Headern (und dem SWIG-Interface) stimmt.
include_directories(${CMAKE_SOURCE_DIR}/bindings)

# Globale SWIG-Flags, ohne -module, da swig_add_library das bereits übernimmt
set(CMAKE_SWIG_FLAGS -package;com.corpuscore.colibri;-outdir;${CMAKE_CURRENT_BINARY_DIR}/java/com/corpuscore/colibri;-I${CMAKE_SOURCE_DIR}/bindings)


# Hier fügst du sowohl deine SWIG-Interface-Datei als auch deinen C-Quellcode ein.
swig_add_library(colibri_java
    LANGUAGE java
    SOURCES
       ${CMAKE_CURRENT_SOURCE_DIR}/colibri.i
       ${CMAKE_CURRENT_SOURCE_DIR}/../colibri.c
)
# Beispiel: Für Android solltest du oft auch die log-Bibliothek verlinken.
#find_library(log-lib log)
target_link_libraries(colibri_java proofer util verifier)

