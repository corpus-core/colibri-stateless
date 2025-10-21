# CMake script to embed HTML file as C string
# Usage: cmake -DINPUT_FILE=config.html -DOUTPUT_FILE=config_html.h -P embed_html.cmake

file(READ ${INPUT_FILE} HTML_CONTENT)

# Escape special characters for C string
string(REPLACE "\\" "\\\\" HTML_CONTENT "${HTML_CONTENT}")
string(REPLACE "\"" "\\\"" HTML_CONTENT "${HTML_CONTENT}")
string(REPLACE "\n" "\\n\"\n\"" HTML_CONTENT "${HTML_CONTENT}")

# Write C header file
file(WRITE ${OUTPUT_FILE} "// Auto-generated from ${INPUT_FILE} - DO NOT EDIT\n")
file(APPEND ${OUTPUT_FILE} "static const char* config_html = \n")
file(APPEND ${OUTPUT_FILE} "\"${HTML_CONTENT}\";\n")

