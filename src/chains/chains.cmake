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

function(add_proofer)
    # Only process if PROOFER is enabled
    if(NOT PROOFER)
        return()
    endif()

    # Parse arguments
    set(options "")
    set(oneValueArgs NAME PROOF)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(PROOFER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Add the library
    add_library(${PROOFER_NAME} STATIC ${PROOFER_SOURCES})
    
    # Set include directories
    target_include_directories(${PROOFER_NAME} PUBLIC ../../proofer proofer)

    
    # Link dependencies
    target_link_libraries(${PROOFER_NAME} PUBLIC ${PROOFER_DEPENDS})
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

# List for all server handler properties
set(SERVER_HANDLER_PROPERTIES "" CACHE INTERNAL "List of all server handler properties")
set(SERVER_HANDLER_LIBS "" CACHE INTERNAL "List of all server handler libraries")

function(add_server_handler)
    if(NOT HTTP_SERVER)
        return()
    endif()

    set(options "")
    set(oneValueArgs NAME INIT_FUNC SHUTDOWN_FUNC GET_DETECTION_REQUEST_FUNC PARSE_VERSION_RESPONSE_FUNC GET_CLIENT_MAPPINGS_FUNC)
    set(multiValueArgs SOURCES DEPENDS)
    cmake_parse_arguments(HANDLER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${HANDLER_NAME} STATIC ${HANDLER_SOURCES})
    target_include_directories(${HANDLER_NAME} PUBLIC 
        ../../server
        ../../
        ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include
        ${CMAKE_BINARY_DIR}/_deps/libuv-src/include
    )
    target_link_libraries(${HANDLER_NAME} PUBLIC libuv llhttp proofer util verifier ${HANDLER_DEPENDS} )
    # target_link_libraries(server PUBLIC ${HANDLER_NAME}) # This is the problematic line

    # Get current list of properties
    get_property(CURRENT_PROPERTIES CACHE SERVER_HANDLER_PROPERTIES PROPERTY VALUE)
    
    # Append the new handler to the list
    list(APPEND CURRENT_PROPERTIES 
         "${HANDLER_NAME}:${HANDLER_INIT_FUNC}:${HANDLER_SHUTDOWN_FUNC}:${HANDLER_GET_DETECTION_REQUEST_FUNC}:${HANDLER_PARSE_VERSION_RESPONSE_FUNC}:${HANDLER_GET_CLIENT_MAPPINGS_FUNC}")
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
        file(APPEND ${SERVER_HANDLERS_H} "void ${init_func}(http_server_t* server);\n")
        file(APPEND ${SERVER_HANDLERS_H} "void ${shutdown_func}(http_server_t* server);\n")
        file(APPEND ${SERVER_HANDLERS_H} "bool ${get_detection_request_func}(data_request_type_t type, const char** path, const char** rpc_payload);\n")
        file(APPEND ${SERVER_HANDLERS_H} "beacon_client_type_t ${parse_version_response_func}(const char* response, data_request_type_t type);\n")
        file(APPEND ${SERVER_HANDLERS_H} "const client_type_mapping_t* ${get_client_mappings_func}();\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "\n")

    # --- Dispatcher-Funktionen generieren ---

    # Init-Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static void c4_server_handlers_init(http_server_t* server) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 1 init_func)
        file(APPEND ${SERVER_HANDLERS_H} "  ${init_func}(server);\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Shutdown-Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static void c4_server_handlers_shutdown(http_server_t* server) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 2 shutdown_func)
        file(APPEND ${SERVER_HANDLERS_H} "  ${shutdown_func}(server);\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Get Detection Request Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static bool c4_server_handlers_get_detection_request(data_request_type_t type, const char** path, const char** rpc_payload) {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 3 get_detection_request_func)
        file(APPEND ${SERVER_HANDLERS_H} "  if (${get_detection_request_func}(type, path, rpc_payload)) return true;\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return false;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Parse Version Response Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static beacon_client_type_t c4_server_handlers_parse_version_response(const char* response, data_request_type_t type) {\n")
    file(APPEND ${SERVER_HANDLERS_H} "  beacon_client_type_t client_type = BEACON_CLIENT_UNKNOWN;\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 4 parse_version_response_func)
        file(APPEND ${SERVER_HANDLERS_H} "  client_type = ${parse_version_response_func}(response, type);\n")
        file(APPEND ${SERVER_HANDLERS_H} "  if (client_type != BEACON_CLIENT_UNKNOWN) return client_type;\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return BEACON_CLIENT_UNKNOWN;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    # Get Client Mappings Dispatcher
    file(APPEND ${SERVER_HANDLERS_H} "static const client_type_mapping_t* c4_server_handlers_get_client_mappings() {\n")
    foreach(prop ${SERVER_HANDLER_PROPERTIES})
        string(REPLACE ":" ";" parts "${prop}")
        list(GET parts 5 get_client_mappings_func)
        file(APPEND ${SERVER_HANDLERS_H} "  const client_type_mapping_t* mappings = ${get_client_mappings_func}();\n")
        file(APPEND ${SERVER_HANDLERS_H} "  if (mappings) return mappings;\n")
    endforeach()
    file(APPEND ${SERVER_HANDLERS_H} "  return NULL;\n")
    file(APPEND ${SERVER_HANDLERS_H} "}\n\n")

    file(APPEND ${SERVER_HANDLERS_H} "#endif // SERVER_HANDLERS_H\n")
endfunction()