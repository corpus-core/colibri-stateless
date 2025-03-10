# List to store all verifier properties
set(VERIFIER_PROPERTIES "" CACHE INTERNAL "List of all verifier properties")

# List to store all proofer properties
set(PROOFER_PROPERTIES "" CACHE INTERNAL "List of all proofer properties")

function(add_verifier)
    # Only process if VERIFIER is enabled
    if(NOT VERIFIER)
        return()
    endif()

    # Parse arguments
    set(options "")
    set(oneValueArgs NAME GET_REQ_TYPE VERIFY)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(VERIFIER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Add the library
    add_library(${VERIFIER_NAME} STATIC ${VERIFIER_SOURCES})
    
    # Set include directories
    target_include_directories(${VERIFIER_NAME} PUBLIC verifier ../../verifier ssz)
    
    # Link dependencies
    target_link_libraries(${VERIFIER_NAME} PUBLIC ${VERIFIER_DEPENDS})
    target_link_libraries(verifier PUBLIC ${VERIFIER_NAME})

    # Get the current global list
    get_property(CURRENT_PROPERTIES CACHE VERIFIER_PROPERTIES PROPERTY VALUE)
    
    # Append to the global list
    list(APPEND CURRENT_PROPERTIES "${VERIFIER_NAME}:${VERIFIER_GET_REQ_TYPE}:${VERIFIER_VERIFY}")
    set(VERIFIER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all verifier properties" FORCE)
endfunction()

function(add_proofer)
    # Only process if PROOFER is enabled
    if(NOT PROOFER)
        return()
    endif()

    # Parse arguments
    set(options "")
    set(oneValueArgs NAME PROOF)
    set(multiValueArgs SOURCES DEPS)
    cmake_parse_arguments(PROOFER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Add the library
    add_library(${PROOFER_NAME} STATIC ${PROOFER_SOURCES})
    
    # Set include directories
    target_include_directories(${PROOFER_NAME} PUBLIC ../../proofer proofer)

    
    # Link dependencies
    target_link_libraries(${PROOFER_NAME} PUBLIC ${PROOFER_DEPS})
    target_link_libraries(proofer PUBLIC ${PROOFER_NAME})

    # Get the current global list
    get_property(CURRENT_PROPERTIES CACHE PROOFER_PROPERTIES PROPERTY VALUE)
    
    # Append to the global list
    list(APPEND CURRENT_PROPERTIES "${PROOFER_NAME}:${PROOFER_PROOF}")
    set(PROOFER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all proofer properties" FORCE)
endfunction()

# Function to generate verifiers.h
function(generate_verifiers_header)
    # Only generate if VERIFIER is enabled
    if(NOT VERIFIER)
        return()
    endif()
    
    set(VERIFIERS_H "${CMAKE_BINARY_DIR}/verifiers.h")
    
    # Start with header guard and includes
    file(WRITE ${VERIFIERS_H} "#ifndef VERIFIERS_H\n")
    file(APPEND ${VERIFIERS_H} "#define VERIFIERS_H\n\n")

    # Add function declarations for each verifier
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 0 name)
        list(GET parts 1 get_req_type)
        list(GET parts 2 verify)
        
        file(APPEND ${VERIFIERS_H} "const ssz_def_t* ${get_req_type}(chain_type_t chain_type);\n")
        file(APPEND ${VERIFIERS_H} "bool ${verify}(verify_ctx_t* ctx);\n\n")
    endforeach()

    # Add request_container function
    file(APPEND ${VERIFIERS_H} "static const ssz_def_t* request_container(chain_type_t chain_type) {\n")
    file(APPEND ${VERIFIERS_H} "  const ssz_def_t* container = NULL;\n")
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 get_req_type)
        file(APPEND ${VERIFIERS_H} "  if (!container) container = ${get_req_type}(chain_type);\n")
    endforeach()
    file(APPEND ${VERIFIERS_H} "  return container;\n")
    file(APPEND ${VERIFIERS_H} "}\n\n")

    # Add handle_verification function
    file(APPEND ${VERIFIERS_H} "static bool handle_verification(verify_ctx_t* ctx) {\n")
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 2 verify)
        file(APPEND ${VERIFIERS_H} "  if (${verify}(ctx)) return true;\n")
    endforeach()
    file(APPEND ${VERIFIERS_H} "  return false;\n")
    file(APPEND ${VERIFIERS_H} "}\n\n")

    # Close header guard
    file(APPEND ${VERIFIERS_H} "#endif // VERIFIERS_H\n")
endfunction()

# Function to generate proofers.h
function(generate_proofers_header)
    # Only generate if PROOFER is enabled
    if(NOT PROOFER)
        return()
    endif()
    
    set(PROOFERS_H "${CMAKE_BINARY_DIR}/proofers.h")
    
    # Start with header guard and includes
    file(WRITE ${PROOFERS_H} "#ifndef PROOFERS_H\n")
    file(APPEND ${PROOFERS_H} "#define PROOFERS_H\n\n")

    # Add function declarations for each proofer
    foreach(prop ${PROOFER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 0 name)
        list(GET parts 1 proof)
        
        file(APPEND ${PROOFERS_H} "bool ${proof}(proofer_ctx_t* ctx);\n\n")
    endforeach()

    # Add proofer_execute function
    file(APPEND ${PROOFERS_H} "static void proofer_execute(proofer_ctx_t* ctx) {\n")
    foreach(prop ${PROOFER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 proof)
        file(APPEND ${PROOFERS_H} "  if (${proof}(ctx)) return;\n")
    endforeach()
    file(APPEND ${PROOFERS_H} "  ctx->state.error = strdup(\"Unsupported chain\");\n")
    file(APPEND ${PROOFERS_H} "}\n\n")

    # Close header guard
    file(APPEND ${PROOFERS_H} "#endif // PROOFERS_H\n")
endfunction()