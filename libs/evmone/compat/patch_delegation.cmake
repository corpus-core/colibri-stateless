# Patch delegation.cpp for incomplete C++20 toolchains (std::ranges::copy).
if(NOT EXISTS ${SOURCE_DIR}/lib/evmone/delegation.cpp)
  message(FATAL_ERROR "delegation.cpp missing; update libs/evmone/compat/patch_delegation.cmake")
endif()

file(READ ${SOURCE_DIR}/lib/evmone/delegation.cpp DELEGATION_CONTENT)

string(FIND "${DELEGATION_CONTENT}" "cpp20_compat.hpp" ALREADY_PATCHED)
string(FIND "${DELEGATION_CONTENT}" "std::ranges::copy" NEEDS_PATCH)

if(ALREADY_PATCHED EQUAL -1 AND NEEDS_PATCH EQUAL -1)
  message(FATAL_ERROR
    "delegation.cpp patch failed: expected std::ranges::copy or existing cpp20_compat include. "
    "Upstream source may have changed; update libs/evmone/compat/patch_delegation.cmake.")
endif()

if(NEEDS_PATCH GREATER -1)
  string(REGEX REPLACE
      "#include \"delegation.hpp\""
      "#include \"delegation.hpp\"\n#include \"cpp20_compat.hpp\""
      TEMP_CONTENT "${DELEGATION_CONTENT}")

  string(REGEX REPLACE
      "std::ranges::copy\\(([^,]+), ([^)]+)\\)"
      "cpp20_compat::ranges_copy(\\1, \\2)"
      PATCHED_CONTENT "${TEMP_CONTENT}")

  file(WRITE ${SOURCE_DIR}/lib/evmone/delegation.cpp "${PATCHED_CONTENT}")
  message(STATUS "Patched delegation.cpp for C++20 compatibility")
else()
  message(STATUS "delegation.cpp already patched for C++20 compatibility")
endif()
