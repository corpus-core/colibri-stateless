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
  // ESP-IDF typically compiles C++ with exceptions disabled. Avoid try/catch in embedded builds.
  // We implement a small base-10/base-16 parser that does not throw.
  if (!value || !str) {
    return 0;
  }

  if (base != 10 && base != 16) {
    intx_init(value);
    return 0;
  }

  const char* p = str;
  if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }

  intx::uint256 acc = 0;
  bool has_digit = false;
  for (; *p; ++p) {
    const char c = *p;
    unsigned v;

    if (base == 10) {
      if (c < '0' || c > '9') {
        intx_init(value);
        return 0;
      }
      v = static_cast<unsigned>(c - '0');
    }
    else {
      if (c >= '0' && c <= '9') {
        v = static_cast<unsigned>(c - '0');
      }
      else if (c >= 'a' && c <= 'f') {
        v = static_cast<unsigned>(10 + (c - 'a'));
      }
      else if (c >= 'A' && c <= 'F') {
        v = static_cast<unsigned>(10 + (c - 'A'));
      }
      else {
        intx_init(value);
        return 0;
      }
    }

    has_digit = true;
    acc = acc * base + v;
  }

  if (!has_digit) {
    intx_init(value);
    return 0;
  }

  to_c(acc, value);
  return 1;
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

// Modular arithmetic operations
void intx_add_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b, const intx_uint256_t* modulus) {
  to_c(intx::addmod(to_cpp(a), to_cpp(b), to_cpp(modulus)), result);
}

void intx_sub_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b, const intx_uint256_t* modulus) {
  intx::uint256 cpp_a = to_cpp(a);
  intx::uint256 cpp_b = to_cpp(b);
  intx::uint256 cpp_m = to_cpp(modulus);

  intx::uint256 diff = cpp_a - cpp_b;
  if (cpp_a < cpp_b) {
    diff += cpp_m;
  }
  to_c(diff, result);
}

void intx_mul_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b, const intx_uint256_t* modulus) {
  to_c(intx::mulmod(to_cpp(a), to_cpp(b), to_cpp(modulus)), result);
}