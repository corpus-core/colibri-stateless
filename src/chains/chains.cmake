# List to store all verifier properties
set(VERIFIER_PROPERTIES "" CACHE INTERNAL "List of all verifier properties")

# List to store all prover properties
set(PROVER_PROPERTIES "" CACHE INTERNAL "List of all prover properties")

function(add_verifier)
    # Only process if VERIFIER is enabled
    if(NOT VERIFIER)
        return()
    endif()

    # Parse arguments
    set(options "")
    set(oneValueArgs NAME GET_REQ_TYPE VERIFY METHOD_TYPE)
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
    list(APPEND CURRENT_PROPERTIES "${VERIFIER_NAME}:${VERIFIER_GET_REQ_TYPE}:${VERIFIER_VERIFY}:${VERIFIER_METHOD_TYPE}")
    set(VERIFIER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all verifier properties" FORCE)
endfunction()

function(add_prover)
    # Only process if PROVER is enabled
    if(NOT PROVER)
        return()
    endif()

    # Parse arguments
    set(options "")
    set(oneValueArgs NAME PROOF)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(PROVER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Add the library
    add_library(${PROVER_NAME} STATIC ${PROVER_SOURCES})
    
    # Set include directories
    target_include_directories(${PROVER_NAME} PUBLIC ../../prover prover)

    
    # Link dependencies
    target_link_libraries(${PROVER_NAME} PUBLIC ${PROVER_DEPENDS})
    target_link_libraries(prover PUBLIC ${PROVER_NAME})

    # Get the current global list
    get_property(CURRENT_PROPERTIES CACHE PROVER_PROPERTIES PROPERTY VALUE)
    
    # Append to the global list
    list(APPEND CURRENT_PROPERTIES "${PROVER_NAME}:${PROVER_PROOF}")
    set(PROVER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all prover properties" FORCE)
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
        list(GET parts 3 method_type)
        file(APPEND ${VERIFIERS_H} "const ssz_def_t* ${get_req_type}(chain_type_t chain_type);\n")
        file(APPEND ${VERIFIERS_H} "bool ${verify}(verify_ctx_t* ctx);\n")
        file(APPEND ${VERIFIERS_H} "method_type_t ${method_type}(chain_id_t chain_id, char* method);\n\n")
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

    # Add request_container function
    file(APPEND ${VERIFIERS_H} "method_type_t c4_get_method_type(chain_id_t chain_id, char* method) {\n")
    file(APPEND ${VERIFIERS_H} "  method_type_t type = METHOD_UNDEFINED;\n")
    foreach(prop ${VERIFIER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 3 method_type)
        file(APPEND ${VERIFIERS_H} "  if (!type) type = ${method_type}(chain_id, method);\n")
    endforeach()
    file(APPEND ${VERIFIERS_H} "  return type;\n")
    file(APPEND ${VERIFIERS_H} "}\n\n")


    # Close header guard
    file(APPEND ${VERIFIERS_H} "#endif // VERIFIERS_H\n")
endfunction()

# Function to generate provers.h
function(generate_provers_header)
    # Only generate if PROVER is enabled
    if(NOT PROVER)
        return()
    endif()
    
    set(PROVERS_H "${CMAKE_BINARY_DIR}/provers.h")
    
    # Start with header guard and includes
    file(WRITE ${PROVERS_H} "#ifndef PROVERS_H\n")
    file(APPEND ${PROVERS_H} "#define PROVERS_H\n\n")

    # Add function declarations for each prover
    foreach(prop ${PROVER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 0 name)
        list(GET parts 1 proof)
        
        file(APPEND ${PROVERS_H} "bool ${proof}(prover_ctx_t* ctx);\n\n")
    endforeach()

    # Add prover_execute function
    file(APPEND ${PROVERS_H} "static void prover_execute(prover_ctx_t* ctx) {\n")
    foreach(prop ${PROVER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 proof)
        file(APPEND ${PROVERS_H} "  if (${proof}(ctx)) return;\n")
    endforeach()
    file(APPEND ${PROVERS_H} "  ctx->state.error = strdup(\"Unsupported chain\");\n")
    file(APPEND ${PROVERS_H} "}\n\n")

    # Close header guard
    file(APPEND ${PROVERS_H} "#endif // PROVERS_H\n")
endfunction()

# List for all server handler properties
set(SERVER_HANDLER_PROPERTIES "" CACHE INTERNAL "List of all server handler properties")
set(SERVER_HANDLER_LIBS "" CACHE INTERNAL "List of all server handler libraries")

function(add_server_handler)
    if(NOT HTTP_SERVER)
        return()
    endif()

    set(options "")
    set(oneValueArgs NAME INIT_FUNC SHUTDOWN_FUNC GET_DETECTION_REQUEST_FUNC PARSE_VERSION_RESPONSE_FUNC GET_CLIENT_MAPPINGS_FUNC METRICS_FUNC)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(HANDLER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${HANDLER_NAME} STATIC ${HANDLER_SOURCES})
    target_include_directories(${HANDLER_NAME} PUBLIC 
        ../../server
        ../../
        ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include
        ${CMAKE_BINARY_DIR}/_deps/libuv-src/include
    )
    
    # Link against CURL since server.h includes curl/curl.h
    find_package(CURL REQUIRED)
    target_link_libraries(${HANDLER_NAME} PUBLIC libuv llhttp prover util verifier CURL::libcurl ${HANDLER_DEPENDS} )
    # target_link_libraries(server PUBLIC ${HANDLER_NAME}) # This is the problematic line

    # Get current list of properties
    get_property(CURRENT_PROPERTIES CACHE SERVER_HANDLER_PROPERTIES PROPERTY VALUE)
    
    # Append the new handler to the list
    list(APPEND CURRENT_PROPERTIES 
         "${HANDLER_NAME}:${HANDLER_INIT_FUNC}:${HANDLER_SHUTDOWN_FUNC}:${HANDLER_GET_DETECTION_REQUEST_FUNC}:${HANDLER_PARSE_VERSION_RESPONSE_FUNC}:${HANDLER_GET_CLIENT_MAPPINGS_FUNC}:${HANDLER_METRICS_FUNC}")
    set(SERVER_HANDLER_PROPERTIES "${CURRENT_PROPERTIES}" CACHE INTERNAL "List of all server handler properties" FORCE)

    # Add the handler library to the global list of libraries
    get_property(CURRENT_LIBS CACHE SERVER_HANDLER_LIBS PROPERTY VALUE)
    list(APPEND CURRENT_LIBS ${HANDLER_NAME})
    set(SERVER_HANDLER_LIBS "${CURRENT_LIBS}" CACHE INTERNAL "List of all server handler libraries" FORCE)
endfunction()

# Function to generate server_handlers.h
function(generate_server_handlers_header)
    if(NOT HTTP_SERVER)
        return()
    endif()

    set(SERVER_HANDLERS_H "${CMAKE_BINARY_DIR}/server_handlers.h")
    
    file(WRITE ${SERVER_HANDLERS_H} "#ifndef SERVER_HANDLERS_H\n")
    file(APPEND ${SERVER_HANDLERS_H} "#define SERVER_HANDLERS_H\n\n")
    file(APPEND ${SERVER_HANDLERS_H} "#include \"server.h\"\n\n")

    # Forward declarations for all handler functions
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 init_func)
        list(GET parts 2 shutdown_func)
        list(GET parts 3 get_detection_request_func)
        list(GET parts 4 parse_version_response_func)
        list(GET parts 5 get_client_mappings_func)
        list(GET parts 6 metrics_func)
        
        # Only declare functions that have non-empty names
        if(NOT "${init_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "void ${init_func}(http_server_t* server);\n")
        endif()
        if(NOT "${shutdown_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "void ${shutdown_func}(http_server_t* server);\n")
        endif()
        if(NOT "${get_detection_request_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "bool ${get_detection_request_func}(http_server_t* server, data_request_type_t type, const char** path, const char** rpc_payload);\n")
        endif()
        if(NOT "${parse_version_response_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "beacon_client_type_t ${parse_version_response_func}(http_server_t* server, const char* response, data_request_type_t type);\n")
        endif()
        if(NOT "${get_client_mappings_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "const client_type_mapping_t* ${get_client_mappings_func}(http_server_t* server);\n")
        endif()
        if(NOT "${metrics_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "void ${metrics_func}(http_server_t* server, buffer_t* data);\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "\n")

    # --- Dispatcher-Funktionen generieren ---

    # Init-Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static void c4_server_handlers_init(http_server_t* server) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 init_func)
        if(NOT "${init_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  ${init_func}(server);\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Shutdown-Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static void c4_server_handlers_shutdown(http_server_t* server) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 2 shutdown_func)
        if(NOT "${shutdown_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  ${shutdown_func}(server);\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Get Detection Request Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static bool c4_server_handlers_get_detection_request(http_server_t* server, data_request_type_t type, const char** path, const char** rpc_payload) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 3 get_detection_request_func)
        if(NOT "${get_detection_request_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  if (${get_detection_request_func}(server, type, path, rpc_payload)) return true;\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return false;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Parse Version Response Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static beacon_client_type_t c4_server_handlers_parse_version_response(http_server_t* server, const char* response, data_request_type_t type) {\n")
    file(APPEND ${SERVER_HANDLERS_H} "  beacon_client_type_t client_type = BEACON_CLIENT_UNKNOWN;\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 4 parse_version_response_func)
        if(NOT "${parse_version_response_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  client_type = ${parse_version_response_func}(server, response, type);\n")
            file(APPEND ${SERVER_HANDLERS_H} "  if (client_type != BEACON_CLIENT_UNKNOWN) return client_type;\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return BEACON_CLIENT_UNKNOWN;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Get Client Mappings Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static const client_type_mapping_t* c4_server_handlers_get_client_mappings(http_server_t* server) {\n  client_type_mapping_t* mappings = NULL;\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 5 get_client_mappings_func)
        if(NOT "${get_client_mappings_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  mappings = ${get_client_mappings_func}(server);\n")
            file(APPEND ${SERVER_HANDLERS_H} "  if (mappings) return mappings;\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return NULL;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Metrics Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static void c4_server_handlers_metrics(http_server_t* server, buffer_t* data) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 6 metrics_func)
        if(NOT "${metrics_func}" STREQUAL "")
            file(APPEND ${SERVER_HANDLERS_H} "  ${metrics_func}(server, data);\n")
        endif()
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    file(APPEND ${SERVER_HANDLERS_H} "#endif // SERVER_HANDLERS_H\n")
endfunction()