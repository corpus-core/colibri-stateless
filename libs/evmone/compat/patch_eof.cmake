# Read source file
file(READ ${SOURCE_DIR}/lib/evmone/eof.cpp EOF_CONTENT)

# Replace all instances of std::get to use std::get_if instead which works on iOS 13.0+
string(REGEX REPLACE 
    "std::get<EOF1Header>\\(error_or_header\\)" 
    "*std::get_if<EOF1Header>(&error_or_header)" 
    TEMP_EOF_CONTENT "${EOF_CONTENT}")
    
string(REGEX REPLACE 
    "std::get<InstructionValidationResult>\\(instr_validation_result_or_error\\)" 
    "*std::get_if<InstructionValidationResult>(&instr_validation_result_or_error)" 
    TEMP_EOF_CONTENT2 "${TEMP_EOF_CONTENT}")
    
string(REGEX REPLACE 
    "std::get<int32_t>\\(msh_or_error\\)" 
    "*std::get_if<int32_t>(&msh_or_error)" 
    TEMP_EOF_CONTENT3 "${TEMP_EOF_CONTENT2}")
    
string(REGEX REPLACE 
    "std::get<EOFSectionHeaders>\\(section_headers_or_error\\)" 
    "*std::get_if<EOFSectionHeaders>(&section_headers_or_error)" 
    PATCHED_EOF_CONTENT "${TEMP_EOF_CONTENT3}")
    
# Generic replacement for other std::get instances we might have missed
string(REGEX REPLACE 
    "std::get<([^>]+)>\\(([^\\)]+)\\)" 
    "*std::get_if<\\1>(&\\2)" 
    FINAL_EOF_CONTENT "${PATCHED_EOF_CONTENT}")

file(WRITE ${SOURCE_DIR}/lib/evmone/eof.cpp "${FINAL_EOF_CONTENT}")
message(STATUS "Patched eof.cpp to handle iOS compatibility") 