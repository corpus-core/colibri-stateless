# Read the source file
file(READ ${SOURCE_DIR}/lib/evmone/execution_state.hpp EXECUTION_STATE_CONTENT)

# Include our fallback header
string(REGEX REPLACE 
    "#include <memory>" 
    "#include <memory>\n#include \"aligned_alloc_fallback.hpp\"" 
    TEMP_CONTENT "${EXECUTION_STATE_CONTENT}")
    
# Replace std::aligned_alloc with evmone_compat::aligned_alloc
string(REGEX REPLACE 
    "std::aligned_alloc\\(alignment, size\\)" 
    "evmone_compat::aligned_alloc(alignment, size)" 
    TEMP_CONTENT2 "${TEMP_CONTENT}")
    
# Replace free with evmone_compat::aligned_free for compatibility
string(REGEX REPLACE 
    "free\\(memory\\)" 
    "evmone_compat::aligned_free(memory)" 
    PATCHED_EXECUTION_STATE_CONTENT "${TEMP_CONTENT2}")
    
file(WRITE ${SOURCE_DIR}/lib/evmone/execution_state.hpp "${PATCHED_EXECUTION_STATE_CONTENT}")
message(STATUS "Patched execution_state.hpp to replace std::aligned_alloc") 