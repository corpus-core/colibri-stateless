#ifndef INTX_C_API_H
#define INTX_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* Define a struct to hold uint256 values directly (no pointers) */
typedef struct {
  unsigned char bytes[32]; /* 256 bits = 32 bytes */
} intx_uint256_t;

/* Initialization functions */
void intx_init(intx_uint256_t* value);
void intx_init_value(intx_uint256_t* value, unsigned long long val);
int  intx_from_string(intx_uint256_t* value, const char* str, int base);

/* Conversion functions */
void intx_to_string(const intx_uint256_t* value, char* output, int output_len, int base);

/* Basic arithmetic operations */
void intx_add(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_sub(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_mul(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_div(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_mod(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);

/* Bitwise operations */
void intx_and(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_or(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_xor(intx_uint256_t* result, const intx_uint256_t* a, const intx_uint256_t* b);
void intx_not(intx_uint256_t* result, const intx_uint256_t* a);
void intx_shl(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift);
void intx_shr(intx_uint256_t* result, const intx_uint256_t* a, unsigned int shift);

/* Comparison operations */
int intx_eq(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_lt(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_gt(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_lte(const intx_uint256_t* a, const intx_uint256_t* b);
int intx_gte(const intx_uint256_t* a, const intx_uint256_t* b);

/* Other useful operations */
void intx_exp(intx_uint256_t* result, const intx_uint256_t* base, const intx_uint256_t* exponent);
int  intx_is_zero(const intx_uint256_t* value);

#ifdef __cplusplus
}
#endif

#endif /* INTX_C_API_H */