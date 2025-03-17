# Read source file
file(READ ${SOURCE_DIR}/lib/evmone/eof.cpp EOF_CONTENT)

# Include our cpp20 compatibility header
string(REGEX REPLACE 
    "#include <algorithm>" 
    "#include <algorithm>\n#include \"cpp20_compat.hpp\"" 
    TEMP_CONTENT "${EOF_CONTENT}")

# If no algorithm header, add it with our compatibility header
string(REGEX MATCH "#include <algorithm>" HAS_ALGORITHM "${TEMP_CONTENT}")
if(NOT HAS_ALGORITHM)
    string(REGEX REPLACE 
        "#include <vector>" 
        "#include <vector>\n#include <algorithm>\n#include \"cpp20_compat.hpp\"" 
        TEMP_CONTENT "${TEMP_CONTENT}")
endif()

# Replace std::ranges::max_element with std::max_element
string(REGEX REPLACE 
    "std::ranges::max_element\\(stack_heights," 
    "std::max_element(stack_heights.begin(), stack_heights.end()," 
    TEMP_CONTENT2 "${TEMP_CONTENT}")

# Replace std::ranges::find with std::find
string(REGEX REPLACE 
    "std::ranges::find\\(visited_code_sections, false\\) != visited_code_sections.end\\(\\)" 
    "std::find(visited_code_sections.begin(), visited_code_sections.end(), false) != visited_code_sections.end()" 
    TEMP_CONTENT3 "${TEMP_CONTENT2}")

# iOS compatibility: Replace std::get with std::get_if
# Replace all instances of std::get to use std::get_if instead which works on iOS 13.0+
string(REGEX REPLACE 
    "std::get<EOF1Header>\\(error_or_header\\)" 
    "*std::get_if<EOF1Header>(&error_or_header)" 
    TEMP_CONTENT4 "${TEMP_CONTENT3}")

string(REGEX REPLACE 
    "std::get<InstructionValidationResult>\\(instr_validation_result_or_error\\)" 
    "*std::get_if<InstructionValidationResult>(&instr_validation_result_or_error)" 
    TEMP_CONTENT5 "${TEMP_CONTENT4}")
    
string(REGEX REPLACE 
    "std::get<int32_t>\\(msh_or_error\\)" 
    "*std::get_if<int32_t>(&msh_or_error)" 
    TEMP_CONTENT6 "${TEMP_CONTENT5}")

string(REGEX REPLACE 
    "std::get<EOFSectionHeaders>\\(section_headers_or_error\\)" 
    "*std::get_if<EOFSectionHeaders>(&section_headers_or_error)" 
    TEMP_CONTENT7 "${TEMP_CONTENT6}")
    
# Generic replacement for other std::get instances we might have missed
string(REGEX REPLACE 
    "std::get<([^>]+)>\\(([^\\)]+)\\)" 
    "*std::get_if<\\1>(&\\2)" 
    PATCHED_CONTENT "${TEMP_CONTENT7}")

file(WRITE ${SOURCE_DIR}/lib/evmone/eof.cpp "${PATCHED_CONTENT}")
message(STATUS "Patched eof.cpp for C++20 and iOS compatibility") 