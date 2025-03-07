# List to store all verifier properties
set(VERIFIER_PROPERTIES "" CACHE INTERNAL "List of all verifier properties")

function(add_verifier)
    # Parse arguments
    set(options "")
    set(oneValueArgs NAME GET_REQ_TYPE VERIFY)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(VERIFIER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Add the library
    add_library(${VERIFIER_NAME} STATIC ${VERIFIER_SOURCES})
    
    # Set include directories
    target_include_directories(${VERIFIER_NAME} PRIVATE ../../util ../../verifier)
    
    # Link dependencies
    target_link_libraries(${VERIFIER_NAME} PRIVATE ${VERIFIER_DEPENDS})
    target_link_libraries(verifier PRIVATE ${VERIFIER_NAME})

    # Get the current global list
    get_property(CURRENT_PROPERTIES CACHE VERIFIER_PROPERTIES PROPERTY VALUE)
    
    # Append to the global list
    list(APPEND CURRENT_PROPERTIES "${VERIFIER_NAME}:${VERIFIER_GET_REQ_TYPE}:${VERIFIER_VERIFY}")
    set(VERIFIER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all verifier properties" FORCE)
endfunction()

# Function to generate verifiers.h
function(generate_verifiers_header)
    set(VERIFIERS_H "${CMAKE_BINARY_DIR}/verifiers.h")
    
    # Define include path for C code
    add_definitions(-DVERIFIERS_PATH="${CMAKE_BINARY_DIR}/verifiers.h")
    
    # Start with header guard and includes
    file(WRITE ${VERIFIERS_H} "#ifndef VERIFIERS_H\n")
    file(APPEND ${VERIFIERS_H} "#define VERIFIERS_H\n\n")
    file(APPEND ${VERIFIERS_H} "#include \"verify.h\"\n\n")

    # Add function declarations for each verifier
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 0 name)
        list(GET parts 1 get_req_type)
        list(GET parts 2 verify)
        
        file(APPEND ${VERIFIERS_H} "ssz_def_t* ${get_req_type}(chain_id_t chain_id);\n")
        file(APPEND ${VERIFIERS_H} "bool ${verify}(verify_ctx_t* ctx);\n\n")
    endforeach()

    # Add request_container function
    file(APPEND ${VERIFIERS_H} "static ssz_def_t* request_container(chain_id_t chain_id) {\n")
    file(APPEND ${VERIFIERS_H} "  ssz_def_t* container = NULL;\n")
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 get_req_type)
        file(APPEND ${VERIFIERS_H} "  if (!container) container = ${get_req_type}(chain_id);\n")
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