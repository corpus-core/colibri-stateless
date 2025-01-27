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

# Füge das Verzeichnis mit den Unit-Tests hinzu
add_subdirectory(unittests)