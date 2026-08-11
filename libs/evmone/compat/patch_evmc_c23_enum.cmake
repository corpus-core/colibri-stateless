# Patch evmc.h for C compilers without C23 support.
#
# EVMC 0.23 declares `enum evmc_access_status : bool` (C23 fixed underlying
# type). MSVC's C compiler and older GCC (e.g. Debian containers) reject this
# syntax when the header is included from C code such as keccak_bridge.c or
# call_evmone.c. Keep the C23 form for C++ and C23-capable compilers and fall
# back to a plain enum otherwise. The enum is only used as a callback return
# type (never as a struct field), so the underlying-type difference does not
# affect any ABI used by Colibri.
file(READ ${SOURCE_DIR}/evmc/include/evmc/evmc.h EVMC_H_CONTENT)

set(C23_ENUM_MARKER "colibri: C23 enum compat")
string(FIND "${EVMC_H_CONTENT}" "${C23_ENUM_MARKER}" ALREADY_PATCHED)
string(FIND "${EVMC_H_CONTENT}" "enum evmc_access_status : bool" HAS_C23_ENUM)

if(ALREADY_PATCHED EQUAL -1 AND HAS_C23_ENUM EQUAL -1)
  message(FATAL_ERROR
    "evmc.h patch failed: expected `enum evmc_access_status : bool`. "
    "Upstream EVMC headers may have changed; update libs/evmone/compat/patch_evmc_c23_enum.cmake.")
endif()

if(ALREADY_PATCHED EQUAL -1)
  string(REPLACE
      "enum evmc_access_status : bool"
      "/* ${C23_ENUM_MARKER}: plain enum for pre-C23 C compilers (MSVC, older GCC). */
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
enum evmc_access_status : bool
#else
enum evmc_access_status
#endif"
      EVMC_H_CONTENT "${EVMC_H_CONTENT}")

  string(FIND "${EVMC_H_CONTENT}" "${C23_ENUM_MARKER}" PATCH_APPLIED)
  if(PATCH_APPLIED EQUAL -1)
    message(FATAL_ERROR
      "evmc.h patch incomplete; update libs/evmone/compat/patch_evmc_c23_enum.cmake.")
  endif()

  file(WRITE ${SOURCE_DIR}/evmc/include/evmc/evmc.h "${EVMC_H_CONTENT}")
  message(STATUS "Patched evmc.h for pre-C23 C compilers")
else()
  message(STATUS "evmc.h already patched for pre-C23 C compilers")
endif()
