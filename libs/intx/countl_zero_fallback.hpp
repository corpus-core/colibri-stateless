#ifndef INTX_COUNTL_ZERO_FALLBACK_HPP
#define INTX_COUNTL_ZERO_FALLBACK_HPP

#include <cstdint>

// Check if we have the countl_zero function from C++20
// We also provide fallback if INTX_ANDROID_COMPAT is defined
#if (defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L) && !defined(INTX_ANDROID_COMPAT)
#include <bit>
#else

// Fallback implementations of countl_zero for various types
namespace std {

inline constexpr int countl_zero(uint8_t x) noexcept {
  if (x == 0) return 8;
  static constexpr int clz_lookup[16] = {
      4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  int n = 0;
  if ((x & 0xF0) == 0) {
    n += 4;
    x <<= 4;
  }
  return n + clz_lookup[x >> 4];
}

inline constexpr int countl_zero(uint16_t x) noexcept {
  if (x == 0) return 16;
  if (x & 0xFF00) return countl_zero(static_cast<uint8_t>(x >> 8));
  return 8 + countl_zero(static_cast<uint8_t>(x));
}

inline constexpr int countl_zero(uint32_t x) noexcept {
  if (x == 0) return 32;
  if (x & 0xFFFF0000) return countl_zero(static_cast<uint16_t>(x >> 16));
  return 16 + countl_zero(static_cast<uint16_t>(x));
}

inline constexpr int countl_zero(uint64_t x) noexcept {
  if (x == 0) return 64;
  if (x & 0xFFFFFFFF00000000) return countl_zero(static_cast<uint32_t>(x >> 32));
  return 32 + countl_zero(static_cast<uint32_t>(x));
}

} // namespace std

#endif // __cpp_lib_bitops check

#endif // INTX_COUNTL_ZERO_FALLBACK_HPP