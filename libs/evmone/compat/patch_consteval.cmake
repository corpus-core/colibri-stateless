# Patch instructions_calls.cpp: replace consteval with constexpr and include compat header
if(EXISTS ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp CALLS_CONTENT)
    
    # Include our cpp20 compatibility header if not already included
    string(REGEX MATCH "#include \"cpp20_compat.hpp\"" HAS_INCLUDE "${CALLS_CONTENT}")
    if(NOT HAS_INCLUDE)
        string(REGEX REPLACE 
            "#include \"instructions.hpp\"" 
            "#include \"instructions.hpp\"\n#include \"cpp20_compat.hpp\"" 
            CALLS_CONTENT "${CALLS_CONTENT}")
    endif()

    # Replace consteval with inline constexpr
    string(REGEX REPLACE 
        "consteval (evmc_call_kind)" 
        "inline constexpr \\1" 
        CALLS_CONTENT "${CALLS_CONTENT}")

    file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp "${CALLS_CONTENT}")
    message(STATUS "Patched instructions_calls.cpp for consteval compatibility")
endif()

# Patch baseline_execution.cpp to include the header
if(EXISTS ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp EXECUTION_CONTENT)
    
    string(REGEX MATCH "#include \"cpp20_compat.hpp\"" HAS_INCLUDE "${EXECUTION_CONTENT}")
    if(NOT HAS_INCLUDE)
        string(REGEX REPLACE 
            "#include \"baseline.hpp\"" 
            "#include \"baseline.hpp\"\n#include \"cpp20_compat.hpp\"" 
            EXECUTION_CONTENT "${EXECUTION_CONTENT}")
        
        file(WRITE ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp "${EXECUTION_CONTENT}")
        message(STATUS "Patched baseline_execution.cpp for consteval compatibility")
    endif()
endif()
