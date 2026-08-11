# Patch create_address.cpp for incomplete C++20 toolchains (std::shift_left).
if(NOT EXISTS ${SOURCE_DIR}/lib/evmone/create_address.cpp)
  message(FATAL_ERROR "create_address.cpp missing; required for evmone >= 0.23")
endif()

file(READ ${SOURCE_DIR}/lib/evmone/create_address.cpp CREATE_CONTENT)

string(FIND "${CREATE_CONTENT}" "cpp20_compat.hpp" ALREADY_PATCHED)
string(FIND "${CREATE_CONTENT}" "std::shift_left" NEEDS_PATCH)

if(ALREADY_PATCHED EQUAL -1 AND NEEDS_PATCH EQUAL -1)
  # Newer upstream may have dropped shift_left; still require the file to look like create_address.
  string(FIND "${CREATE_CONTENT}" "compute_create_address" HAS_CREATE)
  if(HAS_CREATE EQUAL -1)
    message(FATAL_ERROR
      "create_address.cpp patch failed: unexpected content. "
      "Update libs/evmone/compat/patch_create_address.cmake.")
  endif()
  message(STATUS "No std::shift_left in create_address.cpp; skipping C++20 compat patch")
  return()
endif()

if(NEEDS_PATCH GREATER -1)
  string(REGEX REPLACE
      "#include \"create_address.hpp\""
      "#include \"create_address.hpp\"\n#include \"cpp20_compat.hpp\""
      TEMP_CONTENT "${CREATE_CONTENT}")

  string(REGEX REPLACE
      "std::shift_left\\("
      "cpp20_compat::shift_left("
      PATCHED_CONTENT "${TEMP_CONTENT}")

  file(WRITE ${SOURCE_DIR}/lib/evmone/create_address.cpp "${PATCHED_CONTENT}")
  message(STATUS "Patched create_address.cpp for C++20 compatibility")
else()
  message(STATUS "create_address.cpp already patched for C++20 compatibility")
endif()
