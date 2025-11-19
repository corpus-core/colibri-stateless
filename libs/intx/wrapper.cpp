#include "wrapper.hpp"
#include <cstring>
#include <string>

using namespace intx_wrapper;

// Initialization functions
void intx_init(intx_uint256_t* value) {
  intx::uint256 zero = 0;
  to_c(zero, value);
}

void intx_init_value(intx_uint256_t* value, unsigned long long val) {
  intx::uint256 cpp_val = val;
  to_c(cpp_val, value);
}

int intx_from_string(intx_uint256_t* value, const char* str, int base) {
  try {
    // The intx library doesn't support base parameter directly
    // Need to handle hex prefix for base 16
    std::string input_str = str;

    if (base == 16 && input_str.substr(0, 2) != "0x") {
      input_str = "0x" + input_str; // Add 0x prefix for hex if not present
    }
    else if (base != 10) {
      // For other bases, we'd need custom implementation
      // For now, only support base 10 and 16
      intx_init(value);
      return 0;
    }

    intx::uint256 cpp_value = intx::from_string<intx::uint256>(input_str);
    to_c(cpp_value, value);
    return 1; // Success
  } catch (...) {
    intx_init(value); // Set to 0 on error
    return 0;         // Error
  }
}

// Conversion functions
void intx_to_string(const intx_uint256_t* value, char* output, int output_len, int base) {
  intx::uint256 cpp_value = to_cpp(value);
  std::string   str       = intx::to_string(cpp_value, base);

  // Ensure we don't overflow the output buffer
  size_t copy_len = std::min(static_cast<size_t>(output_len - 1), str.length());
  std::memcpy(output, str.c_str(), copy_len);
  output[copy_len] = '\0'; // Null terminate
}

// Basic arithmetic operations
void intx_add(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  intx::uint256 cpp_a      = to_cpp(a);
  intx::uint256 cpp_b      = to_cpp(b);
  intx::uint256 cpp_result = cpp_a + cpp_b;
  to_c(cpp_result, result);
}

void intx_sub(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  intx::uint256 cpp_a      = to_cpp(a);
  intx::uint256 cpp_b      = to_cpp(b);
  intx::uint256 cpp_result = cpp_a - cpp_b;
  to_c(cpp_result, result);
}

void intx_mul(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  intx::uint256 cpp_a      = to_cpp(a);
  intx::uint256 cpp_b      = to_cpp(b);
  intx::uint256 cpp_result = cpp_a * cpp_b;
  to_c(cpp_result, result);
}

void intx_div(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  intx::uint256 cpp_a = to_cpp(a);
  intx::uint256 cpp_b = to_cpp(b);

  if (cpp_b == 0) {
    // Handle division by zero by setting to max value
    to_c(~intx::uint256(0), result);
  }
  else {
    to_c(cpp_a / cpp_b, result);
  }
}

void intx_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  intx::uint256 cpp_a = to_cpp(a);
  intx::uint256 cpp_b = to_cpp(b);

  if (cpp_b == 0) {
    // Handle modulo by zero by setting to 0
    to_c(intx::uint256(0), result);
  }
  else {
    to_c(cpp_a % cpp_b, result);
  }
}

// Bitwise operations
void intx_and(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  to_c(to_cpp(a) & to_cpp(b), result);
}

void intx_or(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  to_c(to_cpp(a) | to_cpp(b), result);
}

void intx_xor(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b) {
  to_c(to_cpp(a) ^ to_cpp(b), result);
}

void intx_not(intx_uint256_t* result, const intx_uint256_t* a) {
  to_c(~to_cpp(a), result);
}

void intx_shl(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift) {
  to_c(to_cpp(a) << shift, result);
}

void intx_shr(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift) {
  to_c(to_cpp(a) >> shift, result);
}

// Comparison operations
int intx_eq(const intx_uint256_t* a, const intx_uint256_t* b) {
  return to_cpp(a) == to_cpp(b);
}

int intx_lt(const intx_uint256_t* a, const intx_uint256_t* b) {
  return to_cpp(a) < to_cpp(b);
}

int intx_gt(const intx_uint256_t* a, const intx_uint256_t* b) {
  return to_cpp(a) > to_cpp(b);
}

int intx_lte(const intx_uint256_t* a, const intx_uint256_t* b) {
  return to_cpp(a) <= to_cpp(b);
}

int intx_gte(const intx_uint256_t* a, const intx_uint256_t* b) {
  return to_cpp(a) >= to_cpp(b);
}

// Other useful operations
void intx_exp(intx_uint256_t* result, const intx_uint256_t* base, const intx_uint256_t* exponent) {
  intx::uint256 b   = to_cpp(base);
  intx::uint256 e   = to_cpp(exponent);
  intx::uint256 res = 1;

  while (e > 0) {
    if (e & 1) {
      res *= b;
    }
    e >>= 1;
    b *= b;
  }

  to_c(res, result);
}

int intx_is_zero(const intx_uint256_t* value) {
  for (int i = 0; i < 32; ++i) {
    if (value->bytes[i] != 0) return 0;
  }
  return 1;
}

// Add this implementation
void intx_modexp(intx_uint256_t* result, const intx_uint256_t* base, const intx_uint256_t* exponent, const intx_uint256_t* modulus) {
  intx::uint256 b = to_cpp(base);
  intx::uint256 e = to_cpp(exponent);
  intx::uint256 m = to_cpp(modulus);

  if (m == 0) {
    to_c(intx::uint256(0), result);
    return;
  }

  intx::uint256 res = 1;
  b                 = b % m;

  while (e > 0) {
    if (e & 1) {
      res = (res * b) % m;
    }
    e >>= 1;
    b = (b * b) % m;
  }

  to_c(res, result);
}

// Add this implementation to wrapper.cpp
void intx_from_bytes(intx_uint256_t* result, const bytes_t bytes) {
  // Clear the result first
  memset(result->bytes, 0, 32);

  // Copy data with proper alignment (big-endian)
  if (bytes.len <= 32) {
    // Small input: right-align in the 32 bytes
    memcpy(result->bytes + (32 - bytes.len), bytes.data, bytes.len);
  }
  else {
    // Input too large: take only the most significant 32 bytes
    memcpy(result->bytes, bytes.data + (bytes.len - 32), 32);
  }
}