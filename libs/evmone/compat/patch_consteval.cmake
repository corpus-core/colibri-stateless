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
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/instructions.hpp "${PATCHED_INSTRUCTIONS}")
    message(STATUS "Patched instructions.hpp for consteval compatibility")
endif()

# Patch baseline_execution.cpp to include the header
if(EXISTS ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp EXECUTION_CONTENT)
    
    # Include our cpp20 compatibility header
    string(REGEX REPLACE 
        "#include \"baseline.hpp\"" 
        "#include \"baseline.hpp\"\n#include \"cpp20_compat.hpp\"" 
        PATCHED_EXECUTION "${EXECUTION_CONTENT}")
    
    file(WRITE ${SOURCE_DIR}/lib/evmone/baseline_execution.cpp "${PATCHED_EXECUTION}")
    message(STATUS "Patched baseline_execution.cpp for consteval compatibility")
endif()

# Patch instructions_calls.cpp to include the header
if(EXISTS ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp CALLS_CONTENT)
    
    # Include our cpp20 compatibility header if not already included
    string(REGEX MATCH "#include \"cpp20_compat.hpp\"" HAS_INCLUDE "${CALLS_CONTENT}")
    if(NOT HAS_INCLUDE)
        string(REGEX REPLACE 
            "#include \"instructions.hpp\"" 
            "#include \"instructions.hpp\"\n#include \"cpp20_compat.hpp\"" 
            PATCHED_CALLS "${CALLS_CONTENT}")
        
        file(WRITE ${SOURCE_DIR}/lib/evmone/instructions_calls.cpp "${PATCHED_CALLS}")
        message(STATUS "Patched instructions_calls.cpp for consteval compatibility")
    endif()
endif() 