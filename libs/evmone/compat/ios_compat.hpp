#pragma once

// This is a compatibility header for iOS to handle C++17 features
// that are only available from iOS 12.0+

#include <optional>
#include <utility>
#include <variant>

#ifdef __APPLE__

// Wrapper for std::optional::value()
namespace ios_compat {
template <typename T>
inline T get_optional_value(const std::optional<T>& opt) {
  return *opt;
}

// Wrapper for std::get<I>(variant)
template <typename T, typename... Types>
inline T& get_variant(std::variant<Types...>& v) {
  return *std::get_if<T>(&v);
}

template <typename T, typename... Types>
inline const T& get_variant(const std::variant<Types...>& v) {
  return *std::get_if<T>(&v);
}
} // namespace ios_compat

#endif