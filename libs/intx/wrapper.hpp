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
  return intx::be::unsafe::load<intx::uint256>(value->bytes);
}

inline void to_c(const intx::uint256& value, intx_uint256_t* result) {
  intx::be::unsafe::store(result->bytes, value);
}
} // namespace intx_wrapper

#ifdef __cplusplus
}
#endif

#endif /* INTX_WRAPPER_HPP */