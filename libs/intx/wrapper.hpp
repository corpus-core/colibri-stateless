#ifndef INTX_WRAPPER_HPP
#define INTX_WRAPPER_HPP

#include "intx_c_api.h"
#include <intx/intx.hpp>

#ifdef __cplusplus
extern "C++" {
#endif

// Internal conversion utilities between C struct and C++ intx
namespace intx_wrapper {
// Convert from C struct to C++ uint256
inline intx::uint256 to_cpp(const intx_uint256_t* value) {
  intx::uint256 result;
  std::memcpy(&result, value->bytes, sizeof(value->bytes));
  return result;
}

// Convert from C++ uint256 to C struct
inline void to_c(const intx::uint256& value, intx_uint256_t* result) {
  std::memcpy(result->bytes, &value, sizeof(result->bytes));
}
} // namespace intx_wrapper

#ifdef __cplusplus
}
#endif

#endif /* INTX_WRAPPER_HPP */