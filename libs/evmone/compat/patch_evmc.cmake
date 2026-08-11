# Patch evmc.hpp for iOS std::optional::value() compatibility.
file(READ ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp EVMC_CONTENT)

string(FIND "${EVMC_CONTENT}" "ios_compat.hpp" ALREADY_PATCHED)
string(FIND "${EVMC_CONTENT}" "from_hex<T>(s).value()" NEEDS_VALUE_PATCH)
string(FIND "${EVMC_CONTENT}" "*from_hex<T>(s)" HAS_VALUE_COMPAT)

if(ALREADY_PATCHED EQUAL -1 AND NEEDS_VALUE_PATCH EQUAL -1 AND HAS_VALUE_COMPAT EQUAL -1)
  message(FATAL_ERROR
    "evmc.hpp patch failed: expected from_hex value()/compat pattern. "
    "Upstream EVMC headers may have changed; update libs/evmone/compat/patch_evmc.cmake.")
endif()

if(NEEDS_VALUE_PATCH GREATER -1 OR (HAS_VALUE_COMPAT GREATER -1 AND ALREADY_PATCHED EQUAL -1))
  set(TEMP_CONTENT "${EVMC_CONTENT}")

  # Prefer an include that exists in EVMC ABI 18 headers.
  if(ALREADY_PATCHED EQUAL -1)
    string(FIND "${TEMP_CONTENT}" "#include <utility>" HAS_UTILITY)
    if(HAS_UTILITY GREATER -1)
      string(REPLACE
          "#include <utility>"
          "#include <utility>\n#ifdef __APPLE__\n#include \"ios_compat.hpp\"\n#endif"
          TEMP_CONTENT "${TEMP_CONTENT}")
    else()
      string(REPLACE
          "#include <evmc/hex.hpp>"
          "#include <evmc/hex.hpp>\n#ifdef __APPLE__\n#include \"ios_compat.hpp\"\n#endif"
          TEMP_CONTENT "${TEMP_CONTENT}")
    endif()
  endif()

  if(NEEDS_VALUE_PATCH GREATER -1)
    string(REGEX REPLACE
        "from_hex<T>\\(s\\)\\.value\\(\\)"
        "*from_hex<T>(s)"
        TEMP_CONTENT "${TEMP_CONTENT}")
  endif()

  string(FIND "${TEMP_CONTENT}" "from_hex<T>(s).value()" STILL_VALUE)
  string(FIND "${TEMP_CONTENT}" "ios_compat.hpp" HAS_COMPAT)
  if(STILL_VALUE GREATER -1 OR HAS_COMPAT EQUAL -1)
    message(FATAL_ERROR
      "evmc.hpp patch incomplete; update libs/evmone/compat/patch_evmc.cmake.")
  endif()

  file(WRITE ${SOURCE_DIR}/evmc/include/evmc/evmc.hpp "${TEMP_CONTENT}")
  message(STATUS "Patched evmc.hpp to handle iOS compatibility")
else()
  message(STATUS "evmc.hpp already patched for iOS compatibility")
endif()
