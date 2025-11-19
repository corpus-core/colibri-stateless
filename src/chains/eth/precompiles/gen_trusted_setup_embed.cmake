# Inputs:
#   -DINPUT=<path to trusted_setup.txt>
#   -DOUTPUT=<path to generated header>
if(NOT DEFINED INPUT)
  message(FATAL_ERROR "INPUT (trusted_setup.txt) not specified")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT header path not specified")
endif()

# Strip potential quotes from paths
string(REPLACE "\"" "" INPUT "${INPUT}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "Trusted setup file not found: ${INPUT}")
endif()

# Read all lines
file(STRINGS "${INPUT}" SETUP_LINES)
list(LENGTH SETUP_LINES LCOUNT)
if(LCOUNT LESS 4)
  message(FATAL_ERROR "Trusted setup file has too few lines")
endif()

# Parse counts
list(GET SETUP_LINES 0 NUM_G1_STR)
list(GET SETUP_LINES 1 NUM_G2_STR)
string(STRIP "${NUM_G1_STR}" NUM_G1_STR)
string(STRIP "${NUM_G2_STR}" NUM_G2_STR)

math(EXPR NUM_G1 "${NUM_G1_STR}")
math(EXPR NUM_G2 "${NUM_G2_STR}")

if(NUM_G2 LESS 2)
  message(FATAL_ERROR "Trusted setup must contain at least 2 G2 points (generator and tau^1)")
endif()

# Compute index of G2[1] line: after 2 header lines and NUM_G1 G1-lines,
# G2[0] is at index (2 + NUM_G1), G2[1] at (2 + NUM_G1 + 1)
math(EXPR IDX_G2_1 "2 + ${NUM_G1} + 1")
if(IDX_G2_1 GREATER_EQUAL LCOUNT)
  message(FATAL_ERROR "Computed G2[1] index is out of range")
endif()

list(GET SETUP_LINES ${IDX_G2_1} G2_TAU_HEX_RAW)
string(STRIP "${G2_TAU_HEX_RAW}" G2_TAU_HEX)

# Validate hex length (expect 96 bytes -> 192 hex chars)
string(LENGTH "${G2_TAU_HEX}" HEXLEN)
if(NOT HEXLEN EQUAL 192)
  message(WARNING "Unexpected G2^tau hex length: ${HEXLEN} (expected 192)")
endif()

# Write header
file(WRITE "${OUTPUT}" "/* Auto-generated: embedded trusted setup (G2^tau) */\n")
file(APPEND "${OUTPUT}" "#ifndef TRUSTED_SETUP_EMBED_H\n")
file(APPEND "${OUTPUT}" "#define TRUSTED_SETUP_EMBED_H\n\n")
file(APPEND "${OUTPUT}" "#include <stdint.h>\n\n")
file(APPEND "${OUTPUT}" "static const unsigned char KZG_G2_TAU_COMPRESSED[96] = {\n")

# Emit bytes as 0x.. comma-separated
set(IDX 0)
while(IDX LESS HEXLEN)
  string(SUBSTRING "${G2_TAU_HEX}" ${IDX} 2 BYTE_HEX)
  math(EXPR IDX "${IDX} + 2")
  # Add comma unless last
  if(IDX LESS HEXLEN)
    file(APPEND "${OUTPUT}" "  0x${BYTE_HEX},\n")
  else()
    file(APPEND "${OUTPUT}" "  0x${BYTE_HEX}\n")
  endif()
endwhile()

file(APPEND "${OUTPUT}" "};\n\n")
file(APPEND "${OUTPUT}" "#endif /* TRUSTED_SETUP_EMBED_H */\n")


