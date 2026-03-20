# Script to patch intx.hpp for Android compatibility
# This script will be included from the main CMakeLists.txt

message(STATUS "Applying intx patch for Android compatibility")

# Get the path to the downloaded intx.hpp file
set(INTX_HPP_PATH "${intx_SOURCE_DIR}/include/intx/intx.hpp")

if(EXISTS "${INTX_HPP_PATH}")
    # --- Pre-pass: whole-file replacements using file(READ)/file(WRITE) ---
    # Must happen BEFORE the line-by-line pass because CMake's file(STRINGS)
    # treats semicolons as list separators, corrupting C++ lines that contain them.
    file(READ "${INTX_HPP_PATH}" _intx_raw)

    # Replace consteval with constexpr globally (NDK clang lacks full consteval)
    string(REPLACE "consteval" "constexpr" _intx_raw "${_intx_raw}")

    # Replace the entire DEFINE_ALIAS_AND_LITERAL macro (multi-line with \ continuations)
    # with a version that only defines the type alias.
    # The literal operators use operator""/consteval which Android NDK clang cannot compile.
    # Regex: match #define DEFINE_ALIAS_AND_LITERAL(N) <first line>\<NL>
    #        then zero or more continuation lines ending with \<NL>
    #        then final line <NL>
    string(REGEX REPLACE
        "#define DEFINE_ALIAS_AND_LITERAL\\(N\\)[^\n]*\\\\\n([^\n]*\\\\\n)*[^\n]*\n"
        "#define DEFINE_ALIAS_AND_LITERAL(N) using uint##N = uint<N>;\n"
        _intx_raw "${_intx_raw}")

    # The simplified macro no longer creates a 'literals' namespace
    string(REPLACE "using namespace literals;" "// using namespace literals;" _intx_raw "${_intx_raw}")

    file(WRITE "${INTX_HPP_PATH}" "${_intx_raw}")
    message(STATUS "Pre-pass: simplified DEFINE_ALIAS_AND_LITERAL for Android")

    # Create the fallback header include line
    set(FALLBACK_INCLUDE "// Include fallback implementation for countl_zero on Android
#include \"${CMAKE_CURRENT_SOURCE_DIR}/countl_zero_fallback.hpp\"
")
    
    # Create the replacement for the C++20 concepts version
    set(REPLACEMENT_FUNCTION "// Replaced C++20 concepts version with explicit overloads for Android compatibility
inline constexpr unsigned clz(uint8_t x) noexcept
{
    return static_cast<unsigned>(std::countl_zero(x));
}

inline constexpr unsigned clz(uint16_t x) noexcept
{
    return static_cast<unsigned>(std::countl_zero(x));
}

inline constexpr unsigned clz(uint32_t x) noexcept
{
    return static_cast<unsigned>(std::countl_zero(x));
}

inline constexpr unsigned clz(uint64_t x) noexcept
{
    return static_cast<unsigned>(std::countl_zero(x));
}")

    # Direct replacement for the entire reciprocal table 
    # This bypasses all the complex REPEAT macros
    # Formula: reciprocal_table_item(d9) = 0x7fd00 / (0x100 | d9) for d9 = 0..255
    set(RECIPROCAL_TABLE_REPLACEMENT "// Direct reciprocal table definition instead of using complex macros
// Formula: reciprocal_table_item(d9) = 0x7fd00 / (0x100 | d9) for d9 = 0..255
constexpr uint16_t reciprocal_table[] = {
    2045, 2037, 2029, 2021, 2013, 2005, 1998, 1990,
    1983, 1975, 1968, 1960, 1953, 1946, 1938, 1931,
    1924, 1917, 1910, 1903, 1896, 1889, 1883, 1876,
    1869, 1863, 1856, 1849, 1843, 1836, 1830, 1824,
    1817, 1811, 1805, 1799, 1792, 1786, 1780, 1774,
    1768, 1762, 1756, 1750, 1745, 1739, 1733, 1727,
    1722, 1716, 1710, 1705, 1699, 1694, 1688, 1683,
    1677, 1672, 1667, 1661, 1656, 1651, 1646, 1641,
    1636, 1630, 1625, 1620, 1615, 1610, 1605, 1600,
    1596, 1591, 1586, 1581, 1576, 1572, 1567, 1562,
    1558, 1553, 1548, 1544, 1539, 1535, 1530, 1526,
    1521, 1517, 1513, 1508, 1504, 1500, 1495, 1491,
    1487, 1483, 1478, 1474, 1470, 1466, 1462, 1458,
    1454, 1450, 1446, 1442, 1438, 1434, 1430, 1426,
    1422, 1418, 1414, 1411, 1407, 1403, 1399, 1396,
    1392, 1388, 1384, 1381, 1377, 1374, 1370, 1366,
    1363, 1359, 1356, 1352, 1349, 1345, 1342, 1338,
    1335, 1332, 1328, 1325, 1322, 1318, 1315, 1312,
    1308, 1305, 1302, 1299, 1295, 1292, 1289, 1286,
    1283, 1280, 1276, 1273, 1270, 1267, 1264, 1261,
    1258, 1255, 1252, 1249, 1246, 1243, 1240, 1237,
    1234, 1231, 1228, 1226, 1223, 1220, 1217, 1214,
    1211, 1209, 1206, 1203, 1200, 1197, 1195, 1192,
    1189, 1187, 1184, 1181, 1179, 1176, 1173, 1171,
    1168, 1165, 1163, 1160, 1158, 1155, 1153, 1150,
    1148, 1145, 1143, 1140, 1138, 1135, 1133, 1130,
    1128, 1125, 1123, 1121, 1118, 1116, 1113, 1111,
    1109, 1106, 1104, 1102, 1099, 1097, 1095, 1092,
    1090, 1088, 1086, 1083, 1081, 1079, 1077, 1074,
    1072, 1070, 1068, 1066, 1064, 1061, 1059, 1057,
    1055, 1053, 1051, 1049, 1047, 1044, 1042, 1040,
    1038, 1036, 1034, 1032, 1030, 1028, 1026, 1024
};")
    
    # Create a new output file path
    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/intx_patched.hpp")
    file(REMOVE "${OUTPUT_FILE}")
    
    # Read the file line by line
    file(STRINGS "${INTX_HPP_PATH}" LINES)
    
    # Patch tracking variables
    set(FALLBACK_INCLUDED FALSE)
    set(REPLACING_FUNCTION FALSE)
    set(FUNCTION_REPLACED FALSE)
    set(RECIPROCAL_TABLE_REPLACED FALSE)
    
    foreach(LINE IN LISTS LINES)
        # Check if we should insert our fallback include
        if(NOT FALLBACK_INCLUDED AND LINE MATCHES "^#include")
            # Write our fallback include before the first real include
            file(APPEND "${OUTPUT_FILE}" "${FALLBACK_INCLUDE}\n")
            set(FALLBACK_INCLUDED TRUE)
        endif()
        
        # Check if this is the line with the problematic C++20 concepts function
        if(LINE MATCHES "inline constexpr unsigned clz\\(std::unsigned_integral auto x\\) noexcept")
            # Start replacing the function
            set(REPLACING_FUNCTION TRUE)
            # Write our replacement
            file(APPEND "${OUTPUT_FILE}" "${REPLACEMENT_FUNCTION}\n")
            set(FUNCTION_REPLACED TRUE)
            # Skip this line
            continue()
        endif()
        
        # If we're in the process of replacing the function, check if we've reached the end
        if(REPLACING_FUNCTION AND LINE MATCHES "}")
            # We've reached the end of the function, stop replacing
            set(REPLACING_FUNCTION FALSE)
            # Skip this line
            continue()
        endif()
        
        # If we're replacing the function, skip all lines until we reach the end
        if(REPLACING_FUNCTION)
            continue()
        endif()
        
        # Check for the reciprocal_table line
        if(NOT RECIPROCAL_TABLE_REPLACED AND LINE MATCHES "constexpr uint16_t reciprocal_table.*REPEAT")
            # Replace the entire reciprocal_table definition
            file(APPEND "${OUTPUT_FILE}" "${RECIPROCAL_TABLE_REPLACEMENT}\n")
            set(RECIPROCAL_TABLE_REPLACED TRUE)
            message(STATUS "Replaced reciprocal_table with direct definition")
            continue()
        endif()
        
        # Otherwise, write the line as is
        file(APPEND "${OUTPUT_FILE}" "${LINE}\n")
    endforeach()
    
    # Check if we were able to replace the function
    if(NOT FUNCTION_REPLACED)
        message(WARNING "Could not find the function to replace in intx.hpp")
    endif()
    
    # Check if we replaced the reciprocal_table
    if(NOT RECIPROCAL_TABLE_REPLACED)
        message(WARNING "Could not find the reciprocal_table to replace")
    endif()
    
    # Replace the original file with our patched version
    file(RENAME "${OUTPUT_FILE}" "${INTX_HPP_PATH}")
    
    message(STATUS "Successfully patched intx.hpp for Android compatibility")
else()
    message(WARNING "Could not find intx.hpp at ${INTX_HPP_PATH}")
endif() 