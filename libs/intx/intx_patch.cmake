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
    
    # Create a new output file path
    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/intx_patched.hpp")
    file(REMOVE "${OUTPUT_FILE}")
    
    # Read the file line by line
    file(STRINGS "${INTX_HPP_PATH}" LINES)
    
    # Insert our fallback include before the first real include
    set(FALLBACK_INCLUDED FALSE)
    set(REPLACING_FUNCTION FALSE)
    set(FUNCTION_REPLACED FALSE)
    
    foreach(LINE IN LISTS LINES)
        # Check if we should insert our fallback include
        if(NOT FALLBACK_INCLUDED AND LINE MATCHES "^#include")
            # Write our fallback include before the first real include
            file(APPEND "${OUTPUT_FILE}" "${FALLBACK_INCLUDE}\n")
            set(FALLBACK_INCLUDED TRUE)
        endif()
        
        # Check if this is the line with the problematic function
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
        
        # Otherwise, write the line as is
        file(APPEND "${OUTPUT_FILE}" "${LINE}\n")
    endforeach()
    
    # Check if we were able to replace the function
    if(NOT FUNCTION_REPLACED)
        message(WARNING "Could not find the function to replace in intx.hpp")
    endif()
    
    # Replace the original file with our patched version
    file(RENAME "${OUTPUT_FILE}" "${INTX_HPP_PATH}")
    
    message(STATUS "Successfully patched intx.hpp for Android compatibility")
else()
    message(WARNING "Could not find intx.hpp at ${INTX_HPP_PATH}")
endif() 