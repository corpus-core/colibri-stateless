# Replace consteval with inline constexpr for toolchains with incomplete C++20 support.

function(c4_patch_consteval_file REL_PATH INCLUDE_ANCHOR)
  set(FULL_PATH ${SOURCE_DIR}/${REL_PATH})
  if(NOT EXISTS ${FULL_PATH})
    message(FATAL_ERROR "consteval patch target missing: ${REL_PATH}")
  endif()

  file(READ ${FULL_PATH} FILE_CONTENT)

  string(FIND "${FILE_CONTENT}" "cpp20_compat.hpp" HAS_INCLUDE)
  string(FIND "${FILE_CONTENT}" "consteval" HAS_CONSTEVAL)

  if(HAS_INCLUDE EQUAL -1 AND HAS_CONSTEVAL EQUAL -1)
    # File may not use consteval; only fail when the include anchor itself disappeared.
    string(FIND "${FILE_CONTENT}" "${INCLUDE_ANCHOR}" HAS_ANCHOR)
    if(HAS_ANCHOR EQUAL -1)
      message(FATAL_ERROR
        "consteval patch failed for ${REL_PATH}: missing include anchor '${INCLUDE_ANCHOR}'. "
        "Upstream source may have changed; update libs/evmone/compat/patch_consteval.cmake.")
    endif()
    message(STATUS "No consteval usage in ${REL_PATH}; skipping")
    return()
  endif()

  if(HAS_INCLUDE EQUAL -1)
    string(REPLACE
        "${INCLUDE_ANCHOR}"
        "${INCLUDE_ANCHOR}\n#include \"cpp20_compat.hpp\""
        FILE_CONTENT "${FILE_CONTENT}")
  endif()

  if(HAS_CONSTEVAL GREATER -1)
    string(REGEX REPLACE
        "consteval "
        "inline constexpr "
        FILE_CONTENT "${FILE_CONTENT}")
  endif()

  file(WRITE ${FULL_PATH} "${FILE_CONTENT}")
  message(STATUS "Patched ${REL_PATH} for consteval compatibility")
endfunction()

c4_patch_consteval_file("lib/evmone/instructions_calls.cpp" "#include \"instructions.hpp\"")
c4_patch_consteval_file("lib/evmone/baseline_execution.cpp" "#include \"baseline.hpp\"")
c4_patch_consteval_file("lib/evmone/baseline_instruction_table.cpp" "#include \"baseline_instruction_table.hpp\"")
