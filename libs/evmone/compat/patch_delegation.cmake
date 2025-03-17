# Read source file
file(READ ${SOURCE_DIR}/lib/evmone/delegation.cpp DELEGATION_CONTENT)

# Include our cpp20 compatibility header
string(REGEX REPLACE 
    "#include <algorithm>" 
    "#include <algorithm>\n#include \"cpp20_compat.hpp\"" 
    TEMP_CONTENT "${DELEGATION_CONTENT}")

# Replace std::ranges::copy with cpp20_compat version
string(REGEX REPLACE 
    "std::ranges::copy\\(designation.substr\\(std::size\\(DELEGATION_MAGIC\\)\\), delegate_address.bytes\\)" 
    "cpp20_compat::ranges_copy(designation.substr(std::size(DELEGATION_MAGIC)), delegate_address.bytes)" 
    PATCHED_CONTENT "${TEMP_CONTENT}")

file(WRITE ${SOURCE_DIR}/lib/evmone/delegation.cpp "${PATCHED_CONTENT}")
message(STATUS "Patched delegation.cpp for C++20 compatibility") 