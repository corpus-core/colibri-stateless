cmake_minimum_required(VERSION 3.5.1)
project(c4)


add_subdirectory(src)
add_subdirectory(libs)

# Define a custom target named 'doc'
add_custom_target(doc ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Generating documentation..."
    COMMAND ${CMAKE_COMMAND} -E env node ${CMAKE_SOURCE_DIR}/scripts/update_doc.js
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts
    COMMENT "Running update_doc.js to generate documentation"
)
