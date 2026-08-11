#pragma once
#include <algorithm>
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

// C++20 compatibility for platforms with incomplete C++20 support (like Android NDK)
namespace cpp20_compat {
// Replacement for std::make_unique_for_overwrite (C++20)
template <typename T>
std::unique_ptr<T> make_unique_for_overwrite(size_t size) {
  return std::unique_ptr<T>(new typename std::remove_extent<T>::type[size]());
}

// Replacement for std::ranges::copy
template <typename InputIt, typename OutputIt>
OutputIt ranges_copy(InputIt first, InputIt last, OutputIt d_first) {
  return std::copy(first, last, d_first);
}

// For container-based copy
template <typename Container, typename OutputIt>
OutputIt ranges_copy(const Container& container, OutputIt d_first) {
  return std::copy(container.begin(), container.end(), d_first);
}

// Replacement for std::shift_left (C++20)
template <typename ForwardIt>
ForwardIt shift_left(ForwardIt first, ForwardIt last, typename std::iterator_traits<ForwardIt>::difference_type n) {
  if (n <= 0) return last;
  const auto dist = std::distance(first, last);
  if (n >= dist) return first;
  auto mid = first;
  std::advance(mid, n);
  return std::move(mid, last, first);
}
} // namespace cpp20_compat

// Define replacements for consteval functions (will be handled in a patch)
#define CONSTEVAL_TEMPLATE inline constexpr
#define CONSTEVAL          inline constexpr
