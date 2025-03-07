#include "intx_c_api.h"
#include <stdio.h>

int main() {
  // Declare stack-based uint256 values
  intx_uint256_t a, b, result;
  char           str_buf[67]; // Max size for 0x prefix + 64 hex digits + null terminator

  // Initialize values
  intx_init(&a);
  intx_init(&b);
  intx_init(&result);

  // Set a to max uint256 value
  intx_from_string(&a, "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 16);

  // Set b to 1
  intx_init_value(&b, 1);

  // Add them
  intx_add(&result, &a, &b);

  // Convert to string and print
  intx_to_string(&result, str_buf, sizeof(str_buf), 16);
  printf("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF + 1 = 0x%s\n", str_buf);

  // Test some EVM operations
  intx_uint256_t c, d, op_result;

  // Initialize more values
  intx_from_string(&c, "0xF0F0F0F0", 16);
  intx_from_string(&d, "0x0F0F0F0F", 16);
  intx_init(&op_result);

  // EVM: AND operation
  intx_and(&op_result, &c, &d);
  intx_to_string(&op_result, str_buf, sizeof(str_buf), 16);
  printf("EVM AND: 0xF0F0F0F0 & 0x0F0F0F0F = 0x%s\n", str_buf);

  // EVM: OR operation
  intx_or(&op_result, &c, &d);
  intx_to_string(&op_result, str_buf, sizeof(str_buf), 16);
  printf("EVM OR: 0xF0F0F0F0 | 0x0F0F0F0F = 0x%s\n", str_buf);

  // EVM: XOR operation
  intx_xor(&op_result, &c, &d);
  intx_to_string(&op_result, str_buf, sizeof(str_buf), 16);
  printf("EVM XOR: 0xF0F0F0F0 ^ 0x0F0F0F0F = 0x%s\n", str_buf);

  // EVM: ADDMOD operation
  intx_uint256_t mod_val;
  intx_from_string(&mod_val, "0x100", 16);
  intx_add(&op_result, &c, &d);
  intx_mod(&op_result, &op_result, &mod_val);
  intx_to_string(&op_result, str_buf, sizeof(str_buf), 16);
  printf("EVM ADDMOD: (0xF0F0F0F0 + 0x0F0F0F0F) %% 0x100 = 0x%s\n", str_buf);

  return 0;
}