# Patch delegation.cpp for C++20 compatibility (std::ranges::copy)
if(EXISTS ${SOURCE_DIR}/lib/evmone/delegation.cpp)
    file(READ ${SOURCE_DIR}/lib/evmone/delegation.cpp DELEGATION_CONTENT)

    # Include our cpp20 compatibility header after the first include
    string(REGEX REPLACE
        "#include \"delegation.hpp\""
        "#include \"delegation.hpp\"\n#include \"cpp20_compat.hpp\""
        TEMP_CONTENT "${DELEGATION_CONTENT}")

    # Replace std::ranges::copy with cpp20_compat version
    string(REGEX REPLACE
        "std::ranges::copy\\(([^,]+), ([^)]+)\\)"
        "cpp20_compat::ranges_copy(\\1, \\2)"
        PATCHED_CONTENT "${TEMP_CONTENT}")

    file(WRITE ${SOURCE_DIR}/lib/evmone/delegation.cpp "${PATCHED_CONTENT}")
    message(STATUS "Patched delegation.cpp for C++20 compatibility")
endif()
