# Script to patch intx.hpp for Android compatibility
# This script will be included from the main CMakeLists.txt

message(STATUS "Applying intx patch for Android compatibility")

# Get the path to the downloaded intx.hpp file
set(INTX_HPP_PATH "${intx_SOURCE_DIR}/include/intx/intx.hpp")

if(EXISTS "${INTX_HPP_PATH}")
    # Read the original file
    file(READ "${INTX_HPP_PATH}" INTX_HPP_CONTENT)
    
    # First, add include for our fallback implementation at the top of the file
    # Find the first include
    string(FIND "${INTX_HPP_CONTENT}" "#include" FIRST_INCLUDE_POS)
    if(FIRST_INCLUDE_POS EQUAL -1)
        message(WARNING "Could not find includes in intx.hpp")
        return()
    endif()
    
    # Get the content before and after the first include
    string(SUBSTRING "${INTX_HPP_CONTENT}" 0 ${FIRST_INCLUDE_POS} CONTENT_BEFORE_INCLUDES)
    string(SUBSTRING "${INTX_HPP_CONTENT}" ${FIRST_INCLUDE_POS} -1 CONTENT_FROM_INCLUDES)
    
    # Add our include
    set(INCLUDE_FALLBACK "// Include fallback implementation for countl_zero on Android
#include \"${CMAKE_CURRENT_SOURCE_DIR}/countl_zero_fallback.hpp\"
")
    
    # Combine parts with our include
    set(INTX_HPP_CONTENT "${CONTENT_BEFORE_INCLUDES}${INCLUDE_FALLBACK}${CONTENT_FROM_INCLUDES}")
    
    # Find the location of the problematic function
    string(FIND "${INTX_HPP_CONTENT}" "inline constexpr unsigned clz(std::unsigned_integral auto x) noexcept" START_POS)
    
    if(START_POS EQUAL -1)
        message(WARNING "Could not find the function to patch. The file format may have changed.")
        return()
    endif()
    
    # Find the end of the function (closing brace)
    string(FIND "${INTX_HPP_CONTENT}" "}" END_POS ${START_POS})
    if(END_POS EQUAL -1)
        message(WARNING "Could not find the end of the function to patch.")
        return()
    endif()
    
    # Count character until the end of line after the closing brace
    string(FIND "${INTX_HPP_CONTENT}" "\n" EOL_POS ${END_POS})
    if(EOL_POS EQUAL -1)
        # If no newline found, use the end of the string
        string(LENGTH "${INTX_HPP_CONTENT}" EOL_POS)
    else()
        # Include the newline in the replaced segment
        math(EXPR EOL_POS "${EOL_POS} + 1")
    endif()
    
    # Calculate the length of the segment to replace
    math(EXPR SEGMENT_LENGTH "${EOL_POS} - ${START_POS}")
    
    # Extract the segment to be replaced
    string(SUBSTRING "${INTX_HPP_CONTENT}" ${START_POS} ${SEGMENT_LENGTH} SEGMENT_TO_REPLACE)
    
    # Create the replacement segment with a comment explaining the patch
    set(REPLACEMENT_SEGMENT "// Replaced C++20 concepts version with explicit overloads for Android compatibility
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
    
    # Replace the segment in the content
    string(REPLACE "${SEGMENT_TO_REPLACE}" "${REPLACEMENT_SEGMENT}" PATCHED_CONTENT "${INTX_HPP_CONTENT}")
    
    # Write the patched content back
    file(WRITE "${INTX_HPP_PATH}" "${PATCHED_CONTENT}")
    
    message(STATUS "Successfully patched intx.hpp for Android compatibility")
else()
    message(WARNING "Could not find intx.hpp at ${INTX_HPP_PATH}")
endif() 