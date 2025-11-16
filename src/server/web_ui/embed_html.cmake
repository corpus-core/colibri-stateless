# CMake script to embed HTML/YAML/text file as C string
# Usage: cmake -DINPUT_FILE=config.html -DOUTPUT_FILE=config_html.h [-DVARIABLE_NAME=variable_name] -P embed_html.cmake

file(READ ${INPUT_FILE} FILE_CONTENT)

# Escape special characters for C string
string(REPLACE "\\" "\\\\" FILE_CONTENT "${FILE_CONTENT}")
string(REPLACE "\"" "\\\"" FILE_CONTENT "${FILE_CONTENT}")
string(REPLACE "\n" "\\n\"\n\"" FILE_CONTENT "${FILE_CONTENT}")

# Determine variable name (default to config_html for backwards compatibility)
if(NOT DEFINED VARIABLE_NAME)
  set(VARIABLE_NAME "config_html")
endif()

# Write C header file
file(WRITE ${OUTPUT_FILE} "// Auto-generated from ${INPUT_FILE} - DO NOT EDIT\n")
file(APPEND ${OUTPUT_FILE} "static const char* ${VARIABLE_NAME} = \n")
file(APPEND ${OUTPUT_FILE} "\"${FILE_CONTENT}\";\n")

