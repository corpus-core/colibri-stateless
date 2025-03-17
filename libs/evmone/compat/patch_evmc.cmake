# Read source file
file(READ ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp EVMC_CONTENT)

# Always include our compatibility header for Apple platforms
string(REGEX REPLACE 
    "#include <optional>" 
    "#include <optional>\n#ifdef __APPLE__\n#include \"ios_compat.hpp\"\n#endif" 
    TEMP_CONTENT "${EVMC_CONTENT}")

# Replace optional::value() calls directly with the operator* approach
string(REGEX REPLACE 
    "from_hex<T>\\(s\\)\\.value\\(\\)" 
    "*from_hex<T>(s)" 
    PATCHED_EVMC_CONTENT "${TEMP_CONTENT}")

file(WRITE ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp "${PATCHED_EVMC_CONTENT}")
message(STATUS "Patched evmc.hpp to handle iOS compatibility") 