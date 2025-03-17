# First, patch the instructions.hpp to replace consteval with constexpr
if(EXISTS ${SOURCE_DIR}/lib/evmone/instructions.hpp)
    file(READ ${SOURCE_DIR}/lib/evmone/instructions.hpp INSTRUCTIONS_CONTENT)
    
    # Include our cpp20 compatibility header
    string(REGEX REPLACE 
        "#include <array>" 
        "#include <array>\n#include \"cpp20_compat.hpp\"" 
        TEMP_CONTENT "${INSTRUCTIONS_CONTENT}")
    
    # Replace consteval with the appropriate macro
    string(REGEX REPLACE 
        "consteval ([^{]*)" 
        "CONSTEVAL \\1" 
        TEMP_CONTENT2 "${TEMP_CONTENT}")
    
    # Replace template consteval with the appropriate macro
    string(REGEX REPLACE 
        "template[^{]*consteval ([^{]*)" 
        "template\\1 CONSTEVAL_TEMPLATE " 
        PATCHED_INSTRUCTIONS "${TEMP_CONTENT2}")
    
    # Make sure all remaining consteval instances are replaced
    string(REGEX REPLACE "consteval" "CONSTEVAL" FINAL_INSTRUCTIONS "${PATCHED_INSTRUCTIONS}")
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/instructions.hpp "${FINAL_INSTRUCTIONS}")
    message(STATUS "Patched instructions.hpp for consteval compatibility")
endif()

# Patch instructions_traits.hpp to replace consteval with constexpr
if(EXISTS ${SOURCE_DIR}/lib/evmone/instructions_traits.hpp)
    file(READ ${SOURCE_DIR}/lib/evmone/instructions_traits.hpp TRAITS_CONTENT)
    
    # Include our cpp20 compatibility header if not already included
    string(REGEX MATCH "#include \"cpp20_compat.hpp\"" HAS_INCLUDE "${TRAITS_CONTENT}")
    if(NOT HAS_INCLUDE)
        string(REGEX REPLACE 
            "#pragma once" 
            "#pragma once\n#include \"cpp20_compat.hpp\"" 
            TEMP_CONTENT "${TRAITS_CONTENT}")
    else()
        set(TEMP_CONTENT "${TRAITS_CONTENT}")
    endif()
    
    # Replace consteval with the appropriate macro
    string(REGEX REPLACE 
        "consteval ([^{]*)" 
        "CONSTEVAL \\1" 
        TEMP_CONTENT2 "${TEMP_CONTENT}")
    
    # Replace template consteval with the appropriate macro
    string(REGEX REPLACE 
        "template[^{]*consteval ([^{]*)" 
        "template\\1 CONSTEVAL_TEMPLATE " 
        PATCHED_TRAITS "${TEMP_CONTENT2}")
    
    # Make sure all remaining consteval instances are replaced
    string(REGEX REPLACE "consteval" "CONSTEVAL" FINAL_TRAITS "${PATCHED_TRAITS}")
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_traits.hpp "${FINAL_TRAITS}")
    message(STATUS "Patched instructions_traits.hpp for consteval compatibility")
endif()

# Patch baseline_execution.cpp to include the header and fix constexpr usage
if(EXISTS ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp EXECUTION_CONTENT)
    
    # Include our cpp20 compatibility header
    string(REGEX REPLACE 
        "#include \"baseline.hpp\"" 
        "#include \"baseline.hpp\"\n#include \"cpp20_compat.hpp\"" 
        TEMP_CONTENT "${EXECUTION_CONTENT}")
    
    # Replace consteval function calls with constexpr equivalents
    string(REGEX REPLACE 
        "if constexpr \\(!instr::has_const_gas_cost\\(Op\\)\\)" 
        "if constexpr (!CONSTEVAL instr::has_const_gas_cost(Op))" 
        PATCHED_EXECUTION "${TEMP_CONTENT}")
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp "${PATCHED_EXECUTION}")
    message(STATUS "Patched baseline_execution.cpp for consteval compatibility")
endif()

# Patch instructions_calls.cpp to include the header and fix constexpr usage
if(EXISTS ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp CALLS_CONTENT)
    
    # Include our cpp20 compatibility header if not already included
    string(REGEX MATCH "#include \"cpp20_compat.hpp\"" HAS_INCLUDE "${CALLS_CONTENT}")
    if(NOT HAS_INCLUDE)
        string(REGEX REPLACE 
            "#include \"instructions.hpp\"" 
            "#include \"instructions.hpp\"\n#include \"cpp20_compat.hpp\"" 
            TEMP_CONTENT "${CALLS_CONTENT}")
    else()
        set(TEMP_CONTENT "${CALLS_CONTENT}")
    endif()
    
    # Replace to_call_kind calls with constexpr equivalent
    string(REGEX REPLACE 
        "evmc_message msg\\{.kind = to_call_kind\\(Op\\)\\}" 
        "evmc_message msg{.kind = CONSTEVAL to_call_kind(Op)}" 
        PATCHED_CALLS "${TEMP_CONTENT}")
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp "${PATCHED_CALLS}")
    message(STATUS "Patched instructions_calls.cpp for consteval compatibility")
endif() 