find_program(GO_EXECUTABLE go)
if (NOT GO_EXECUTABLE)
  message(FATAL_ERROR "Go nicht gefunden. Bitte Go installieren und in PATH bereitstellen.")
endif()

file(GLOB_RECURSE GO_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.go"
)

set(OPG_BRIDGE_BIN "${CMAKE_BINARY_DIR}/bin/opg_bridge${CMAKE_EXECUTABLE_SUFFIX}")

set(_go_env "")
# if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
#   list(APPEND _go_env "GOOS=windows" "GOARCH=amd64")
# endif()

add_custom_command(
  OUTPUT "${OPG_BRIDGE_BIN}"
  BYPRODUCTS "${OPG_BRIDGE_BIN}"
  COMMAND ${CMAKE_COMMAND} -E env ${_go_env}
          ${GO_EXECUTABLE} build
          -o "${OPG_BRIDGE_BIN}"
          .
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS ${GO_SOURCES}
  COMMENT "build Go-Bridge: opg_bridge"
  VERBATIM
)

add_custom_target(opg_bridge ALL
  DEPENDS "${OPG_BRIDGE_BIN}"
)

# Installation of the Go-Binary
install(PROGRAMS "${OPG_BRIDGE_BIN}" DESTINATION bin)