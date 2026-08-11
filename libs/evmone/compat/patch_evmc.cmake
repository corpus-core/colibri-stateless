# Patch evmc.hpp for iOS std::optional::value() compatibility.
file(READ ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp EVMC_CONTENT)

string(FIND "${EVMC_CONTENT}" "ios_compat.hpp" ALREADY_PATCHED)
string(FIND "${EVMC_CONTENT}" "from_hex<T>(s).value()" NEEDS_PATCH)

if(ALREADY_PATCHED EQUAL -1 AND NEEDS_PATCH EQUAL -1)
  message(FATAL_ERROR
    "evmc.hpp patch failed: expected from_hex<T>(s).value() or existing ios_compat.hpp include. "
    "Upstream EVMC headers may have changed; update libs/evmone/compat/patch_evmc.cmake.")
endif()

if(NEEDS_PATCH GREATER -1)
  string(REGEX REPLACE
      "#include <optional>"
      "#include <optional>\n#ifdef __APPLE__\n#include \"ios_compat.hpp\"\n#endif"
      TEMP_CONTENT "${EVMC_CONTENT}")

  string(REGEX REPLACE
      "from_hex<T>\\(s\\)\\.value\\(\\)"
      "*from_hex<T>(s)"
      PATCHED_EVMC_CONTENT "${TEMP_CONTENT}")

  file(WRITE ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp "${PATCHED_EVMC_CONTENT}")
  message(STATUS "Patched evmc.hpp to handle iOS compatibility")
else()
  message(STATUS "evmc.hpp already patched for iOS compatibility")
endif()
