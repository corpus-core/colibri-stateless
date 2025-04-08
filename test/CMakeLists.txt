# Erforderlich für FetchContent
include(FetchContent)

# Definiere das FetchContent-Objekt für Unity
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2  # Verwende die gewünschte Unity-Version
)

# Lade und bereite Unity vor
FetchContent_MakeAvailable(unity)

# Suppress MSVC warning C4711 for unity target
if(MSVC)
    target_compile_options(unity PRIVATE 
        /wd4711 # automatic inline expansion
        /wd4061 # enumerator in switch of enum is not explicitly handled
        /wd5045 # Spectre mitigation info
    )
endif()

# Füge das Verzeichnis mit den Unit-Tests hinzu
add_subdirectory(unittests)