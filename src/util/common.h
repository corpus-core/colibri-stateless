#ifndef C4_VISIBILITY_H // Include guard for the header file
#define C4_VISIBILITY_H

// Check for GCC or Clang (which often defines __GNUC__ too)
#if defined(__GNUC__) && (__GNUC__ >= 4)
// Visibility("hidden") is available starting GCC 4
#define INTERNAL   __attribute__((visibility("hidden")))
#define API_PUBLIC __attribute__((visibility("default"))) // Optional: Explicitly mark public API
#elif defined(__GNUC__)                                   // Older GCC/Clang without visibility support
#define INTERNAL
#define API_PUBLIC
#elif defined(_MSC_VER)
// MSVC doesn't have a direct equivalent for 'hidden' for static libs.
// Rely on 'static' keyword for file-local symbols.
// Rely on prefixes for internal symbols shared between .c files.
#define INTERNAL
// For DLLs, you'd use __declspec(dllexport/dllimport) here for API_PUBLIC
#define API_PUBLIC
#else // Other compilers
#define INTERNAL
#define API_PUBLIC
#endif

#include <stdint.h>

static inline uint64_t min64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static inline uint64_t max64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

static inline uint64_t clamp64(uint64_t value, uint64_t min, uint64_t max) {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

#endif // C4_VISIBILITY_H