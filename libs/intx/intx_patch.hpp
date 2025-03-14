// Patched clz implementation to avoid using C++20 concepts
// Replace the original implementation with template specializations

#include <bit>     // For std::countl_zero in C++20
#include <cstdint> // For uint8_t, uint16_t, uint32_t, uint64_t

// If we're on older compilers without std::countl_zero, provide a fallback
#if !defined(__cpp_lib_bitops) || __cpp_lib_bitops < 201907L
namespace std {
// Simple implementation of countl_zero for fallback
inline constexpr int countl_zero(uint8_t x) noexcept {
  if (x == 0) return 8;
  int n = 0;
  if ((x & 0xF0) == 0) {
    n += 4;
    x <<= 4;
  }
  if ((x & 0xC0) == 0) {
    n += 2;
    x <<= 2;
  }
  if ((x & 0x80) == 0) { n += 1; }
  return n;
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
#endif

// For uint8_t
inline constexpr unsigned clz(uint8_t x) noexcept {
  return static_cast<unsigned>(std::countl_zero(x));
}

// For uint16_t
inline constexpr unsigned clz(uint16_t x) noexcept {
  return static_cast<unsigned>(std::countl_zero(x));
}

// For uint32_t
inline constexpr unsigned clz(uint32_t x) noexcept {
  return static_cast<unsigned>(std::countl_zero(x));
}

// For uint64_t
inline constexpr unsigned clz(uint64_t x) noexcept {
  return static_cast<unsigned>(std::countl_zero(x));
}

// Forward declare uint128 to match intx library
namespace intx {
struct uint128;
}
using uint128 = intx::uint128;

// For uint128
inline constexpr unsigned clz(uint128 x) noexcept {
  // In this order `h == 0` we get less instructions than in case of `h != 0`.
  return x[1] == 0 ? clz(x[0]) + 64 : clz(x[1]);
}