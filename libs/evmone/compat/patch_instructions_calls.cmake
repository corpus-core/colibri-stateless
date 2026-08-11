# Patch instructions_calls.cpp: avoid std::get<> for iOS libstdc++/variant quirks.
file(READ ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp CALLS_CONTENT)

string(FIND "${CALLS_CONTENT}" "std::get_if<evmc::address>(&target_addr_or_result)" ALREADY_PATCHED)
string(FIND "${CALLS_CONTENT}" "std::get<evmc::address>(target_addr_or_result)" NEEDS_PATCH)

if(ALREADY_PATCHED EQUAL -1 AND NEEDS_PATCH EQUAL -1)
  message(FATAL_ERROR
    "instructions_calls.cpp patch failed: expected std::get<evmc::address>(...) pattern. "
    "Upstream source may have changed; update libs/evmone/compat/patch_instructions_calls.cmake.")
endif()

if(NEEDS_PATCH GREATER -1)
  string(REGEX REPLACE
      "const auto& code_addr = std::get<evmc::address>\\(target_addr_or_result\\)"
      "const auto* addr_ptr = std::get_if<evmc::address>(&target_addr_or_result);\n    const auto& code_addr = *addr_ptr"
      PATCHED_CALLS_CONTENT "${CALLS_CONTENT}")

  file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp "${PATCHED_CALLS_CONTENT}")
  message(STATUS "Patched instructions_calls.cpp to handle iOS compatibility")
else()
  message(STATUS "instructions_calls.cpp already patched for iOS compatibility")
endif()
