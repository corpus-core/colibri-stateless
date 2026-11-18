# Copyright (c) 2025,2026 corpus.core GmbH
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

if(CHAIN_ETH)
    add_subdirectory(eth)
endif()
if(CHAIN_OP)
    add_subdirectory(op)
endif()
# EEZ rollups are proven through their L1 and reuse the Ethereum modules.
if(CHAIN_EEZ AND CHAIN_ETH)
    add_subdirectory(eez)
endif()
# Arbitrum reuses the Ethereum prover and verifier. Off unless the preset or
# the caller turns CHAIN_ARB on; the feed client is not stable yet.
if(CHAIN_ARB AND CHAIN_ETH)
    add_subdirectory(arb)
elseif(CHAIN_ARB)
    message(STATUS "CHAIN_ARB requires CHAIN_ETH; Arbitrum module skipped")
endif()

# ---------------------------------------------------------------------------
# EXTRA_CHAIN: third-party chain plugins pulled in by this build.
#
# A comma-separated list of either local directories or git URLs (optionally
# with `#ref` to pin a branch or tag). Example:
#
#   cmake -DEXTRA_CHAIN=/path/to/polygon-colibri,https://example.com/org/x.git#v1
#
# Each entry is `add_subdirectory`d here, before `src/server` is configured by
# the enclosing `src/CMakeLists.txt`. That order matters: `add_server_handler`
# only lands in `SERVER_HANDLER_LIBS` (and the generated `server_handlers.h`)
# while this subdirectory is being processed. The top-level `INCLUDE` variable
# is applied too late for that.
#
# Plugin repos run with the build's own privileges. Only point `EXTRA_CHAIN`
# at code you trust.
# ---------------------------------------------------------------------------
set(EXTRA_CHAIN "" CACHE STRING "Comma-separated list of third-party chain plugins (local dir or git URL, optionally with #ref)")

if(EXTRA_CHAIN)
    # Normalise separators: both commas and semicolons produce a CMake list.
    string(REPLACE "," ";" _c4_extra_chain_list "${EXTRA_CHAIN}")

    foreach(_c4_entry IN LISTS _c4_extra_chain_list)
        string(STRIP "${_c4_entry}" _c4_entry)
        if("${_c4_entry}" STREQUAL "")
            continue()
        endif()

        # Split optional "#ref" suffix (git branch or tag).
        set(_c4_entry_ref "")
        string(FIND "${_c4_entry}" "#" _c4_hash_pos)
        if(NOT _c4_hash_pos EQUAL -1)
            string(SUBSTRING "${_c4_entry}" 0 ${_c4_hash_pos} _c4_entry_path)
            math(EXPR _c4_hash_start "${_c4_hash_pos} + 1")
            string(SUBSTRING "${_c4_entry}" ${_c4_hash_start} -1 _c4_entry_ref)
            set(_c4_entry "${_c4_entry_path}")
        endif()

        # Classify: existing directory, or a git URL we need to clone.
        set(_c4_plugin_dir "")
        if(EXISTS "${_c4_entry}" AND IS_DIRECTORY "${_c4_entry}")
            set(_c4_plugin_dir "${_c4_entry}")
        else()
            # Derive a target directory name from the URL (strip `.git`, keep tail).
            string(REGEX REPLACE "\\.git$" "" _c4_url_noext "${_c4_entry}")
            string(REGEX REPLACE ".*/" ""       _c4_url_name "${_c4_url_noext}")
            if("${_c4_url_name}" STREQUAL "")
                message(FATAL_ERROR "EXTRA_CHAIN: cannot derive a directory name from '${_c4_entry}'")
            endif()

            set(_c4_plugin_dir "${CMAKE_BINARY_DIR}/extra_chains/${_c4_url_name}")

            if(NOT EXISTS "${_c4_plugin_dir}")
                find_package(Git QUIET REQUIRED)
                file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/extra_chains")

                set(_c4_clone_args clone --depth 1)
                if(NOT "${_c4_entry_ref}" STREQUAL "")
                    list(APPEND _c4_clone_args --branch "${_c4_entry_ref}")
                endif()
                list(APPEND _c4_clone_args "${_c4_entry}" "${_c4_plugin_dir}")

                message(STATUS "EXTRA_CHAIN: cloning ${_c4_entry} -> ${_c4_plugin_dir}")
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" ${_c4_clone_args}
                    RESULT_VARIABLE _c4_clone_result
                    OUTPUT_VARIABLE _c4_clone_output
                    ERROR_VARIABLE  _c4_clone_output
                )
                if(NOT _c4_clone_result EQUAL 0)
                    message(FATAL_ERROR
                        "EXTRA_CHAIN: git clone of '${_c4_entry}' failed (exit ${_c4_clone_result}):\n${_c4_clone_output}")
                endif()
            else()
                message(STATUS "EXTRA_CHAIN: reusing ${_c4_plugin_dir}")
            endif()
        endif()

        if(NOT EXISTS "${_c4_plugin_dir}/CMakeLists.txt")
            message(FATAL_ERROR
                "EXTRA_CHAIN: '${_c4_plugin_dir}' has no CMakeLists.txt")
        endif()

        # Each plugin lives outside `src/chains/`; place its build artefacts
        # under a dedicated binary subdirectory so target names do not clash.
        get_filename_component(_c4_plugin_name "${_c4_plugin_dir}" NAME)
        message(STATUS "EXTRA_CHAIN: adding ${_c4_plugin_name} (${_c4_plugin_dir})")
        add_subdirectory("${_c4_plugin_dir}" "${CMAKE_BINARY_DIR}/extra_chains/_build_${_c4_plugin_name}")
    endforeach()
endif()
