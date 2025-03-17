#pragma once
#include <cstdlib>
#include <memory>

#if defined(_WIN32) || defined(_MSC_VER)
#include <malloc.h> // for _aligned_malloc/_aligned_free
#endif

namespace evmone_compat {
inline void* aligned_alloc(size_t alignment, size_t size) {
  // Platform-specific implementations
  void* ptr = nullptr;

#if defined(_WIN32) || defined(_MSC_VER)
  // Windows implementation using _aligned_malloc
  ptr = _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__ANDROID__) || defined(ANDROID)
  // Apple/Android implementation using posix_memalign
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return nullptr;
  }
#else
  // Standard C11 implementation
  return std::aligned_alloc(alignment, size);
#endif

  return ptr;
}

#if defined(_WIN32) || defined(_MSC_VER)
// Custom aligned free for Windows
inline void aligned_free(void* ptr) {
  _aligned_free(ptr);
}
#else
// Use regular free for other platforms
inline void aligned_free(void* ptr) {
  free(ptr);
}
#endif
} // namespace evmone_compat