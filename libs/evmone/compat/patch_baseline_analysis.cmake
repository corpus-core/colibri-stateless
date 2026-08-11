# Patch baseline_analysis.cpp for incomplete C++20 toolchains.
file(READ ${SOURCE_DIR}/lib/evmone/baseline_analysis.cpp BASELINE_ANALYSIS_CONTENT)

string(FIND "${BASELINE_ANALYSIS_CONTENT}" "std::make_unique_for_overwrite" NEEDS_UNIQUE)
string(FIND "${BASELINE_ANALYSIS_CONTENT}" "std::ranges::copy" NEEDS_RANGES)
string(FIND "${BASELINE_ANALYSIS_CONTENT}" "cpp20_compat::make_unique_for_overwrite" HAS_UNIQUE_COMPAT)
string(FIND "${BASELINE_ANALYSIS_CONTENT}" "cpp20_compat::ranges_copy" HAS_RANGES_COMPAT)

if(NEEDS_UNIQUE EQUAL -1 AND NEEDS_RANGES EQUAL -1 AND HAS_UNIQUE_COMPAT EQUAL -1 AND HAS_RANGES_COMPAT EQUAL -1)
  message(FATAL_ERROR
    "baseline_analysis.cpp patch failed: expected C++20 APIs or existing cpp20_compat replacements. "
    "Upstream source may have changed; update libs/evmone/compat/patch_baseline_analysis.cmake.")
endif()

if(NEEDS_UNIQUE GREATER -1 OR NEEDS_RANGES GREATER -1)
  string(FIND "${BASELINE_ANALYSIS_CONTENT}" "cpp20_compat.hpp" HAS_INCLUDE)
  set(TEMP_CONTENT "${BASELINE_ANALYSIS_CONTENT}")
  if(HAS_INCLUDE EQUAL -1)
    string(REGEX REPLACE
        "#include <memory>"
        "#include <memory>\n#include \"cpp20_compat.hpp\""
        TEMP_CONTENT "${TEMP_CONTENT}")
  endif()

  string(REGEX REPLACE
      "std::make_unique_for_overwrite<uint8_t\\[\\]>\\(([^)]+)\\)"
      "cpp20_compat::make_unique_for_overwrite<uint8_t[]>(\\1)"
      TEMP_CONTENT "${TEMP_CONTENT}")

  string(REGEX REPLACE
      "std::ranges::copy\\(([^,]+), ([^)]+)\\)"
      "cpp20_compat::ranges_copy(\\1, \\2)"
      TEMP_CONTENT "${TEMP_CONTENT}")

  string(FIND "${TEMP_CONTENT}" "std::make_unique_for_overwrite" STILL_UNIQUE)
  string(FIND "${TEMP_CONTENT}" "std::ranges::copy" STILL_RANGES)
  if(STILL_UNIQUE GREATER -1 OR STILL_RANGES GREATER -1)
    message(FATAL_ERROR
      "baseline_analysis.cpp patch did not replace all C++20 APIs; "
      "update libs/evmone/compat/patch_baseline_analysis.cmake.")
  endif()

  file(WRITE ${SOURCE_DIR}/lib/evmone/baseline_analysis.cpp "${TEMP_CONTENT}")
  message(STATUS "Patched baseline_analysis.cpp for C++20 compatibility")
else()
  message(STATUS "baseline_analysis.cpp already patched for C++20 compatibility")
endif()
