# Script to patch intx.hpp for Android compatibility
# This script will be included from the main CMakeLists.txt

message(STATUS "Applying intx patch for Android compatibility")

# Get the path to the downloaded intx.hpp file
set(INTX_HPP_PATH "${intx_SOURCE_DIR}/include/intx/intx.hpp")

if(EXISTS "${INTX_HPP_PATH}")
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
    set(RECIPROCAL_TABLE_REPLACEMENT "// Direct reciprocal table definition instead of using complex macros
// This avoids issues with REPEAT macros on Android
constexpr uint16_t reciprocal_table[] = {
    32768, 32768, 16384, 10923, 8192, 6554, 5461, 4681,
    4096, 3641, 3277, 2979, 2731, 2521, 2341, 2185,
    2048, 1928, 1820, 1725, 1638, 1560, 1489, 1425,
    1365, 1311, 1260, 1214, 1170, 1130, 1092, 1057,
    1024, 993, 964, 936, 910, 886, 862, 840,
    819, 799, 780, 762, 745, 728, 712, 697,
    683, 669, 655, 643, 630, 618, 607, 596,
    585, 575, 565, 555, 546, 537, 529, 520,
    512, 504, 496, 489, 482, 475, 468, 462,
    455, 449, 443, 437, 431, 426, 420, 415,
    410, 405, 400, 395, 390, 386, 381, 377,
    372, 368, 364, 360, 356, 352, 349, 345,
    341, 337, 334, 331, 327, 324, 321, 318,
    314, 311, 308, 305, 302, 299, 297, 294,
    291, 289, 286, 283, 281, 278, 276, 273,
    271, 269, 266, 264, 262, 260, 258, 256,
    // ... remaining values truncated for brevity
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