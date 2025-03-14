// Patched clz implementation to avoid using C++20 concepts
// Replace the original implementation with template specializations

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

// For uint128
inline constexpr unsigned clz(uint128 x) noexcept {
  // In this order `h == 0` we get less instructions than in case of `h != 0`.
  return x[1] == 0 ? clz(x[0]) + 64 : clz(x[1]);
}