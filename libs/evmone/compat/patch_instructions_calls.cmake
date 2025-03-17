# Read source file
file(READ ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp CALLS_CONTENT)

# First patch the specific instances we're seeing in the errors
string(REGEX REPLACE 
    "const auto& code_addr = std::get<evmc::address>\\(target_addr_or_result\\)" 
    "const auto* addr_ptr = std::get_if<evmc::address>(&target_addr_or_result);\n    const auto& code_addr = *addr_ptr" 
    PATCHED_CALLS_CONTENT "${CALLS_CONTENT}")

file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp "${PATCHED_CALLS_CONTENT}")
message(STATUS "Patched instructions_calls.cpp to handle iOS compatibility") 