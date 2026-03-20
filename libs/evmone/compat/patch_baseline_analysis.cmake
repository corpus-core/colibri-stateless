# Read source file
file(READ ${SOURCE_DIR}/lib/evmone/baseline_analysis.cpp BASELINE_ANALYSIS_CONTENT)

# Include our cpp20 compatibility header
string(REGEX REPLACE 
    "#include <memory>" 
    "#include <memory>\n#include \"cpp20_compat.hpp\"" 
    TEMP_CONTENT "${BASELINE_ANALYSIS_CONTENT}")

# Replace std::make_unique_for_overwrite with cpp20_compat version (generic argument)
string(REGEX REPLACE 
    "std::make_unique_for_overwrite<uint8_t\\[\\]>\\(([^)]+)\\)" 
    "cpp20_compat::make_unique_for_overwrite<uint8_t[]>(\\1)" 
    TEMP_CONTENT2 "${TEMP_CONTENT}")

# Replace std::ranges::copy with cpp20_compat version (generic arguments)
string(REGEX REPLACE 
    "std::ranges::copy\\(([^,]+), ([^)]+)\\)" 
    "cpp20_compat::ranges_copy(\\1, \\2)" 
    PATCHED_CONTENT "${TEMP_CONTENT2}")

file(WRITE ${SOURCE_DIR}/lib/evmone/baseline_analysis.cpp "${PATCHED_CONTENT}")
message(STATUS "Patched baseline_analysis.cpp for C++20 compatibility")
