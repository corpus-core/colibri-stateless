#include "rlp.h"

static int check_range(bytes_t* target, bytes_t* src, size_t new_len, uint8_t* new_start, rlp_type_t result_type) {
  if (!target) return RLP_OUT_OF_RANGE;
  *target = bytes(new_start, new_len);
  return (new_start >= src->data && (new_start + new_len) >= src->data && (new_start + new_len) <= (src->data + src->len)) ? result_type : RLP_OUT_OF_RANGE;
}

rlp_type_t rlp_decode(bytes_t* src, int index, bytes_t* target) {
  size_t pos = 0, src_idx = 0;
  for (; src_idx < src->len; src_idx++, pos++) {
    uint8_t c = src->data[src_idx];
    if (c < 0x80) {
      if ((int) pos == index)
        return check_range(target, src, 1, src->data + src_idx, 1);
    }
    else if (c < 0xb8) {
      if ((int) pos == index)
        return check_range(target, src, c - 0x80, src->data + src_idx + 1, RLP_ITEM);
      src_idx += c - 0x80;
    }
    else if (c < 0xc0) {
      size_t len, n;
      for (len = 0, n = 0; n < (uint8_t) (c - 0xB7); n++)
        len |= (*(src->data + src_idx + 1 + n)) << (8 * ((c - 0xb7) - n - 1));
      if ((int) pos == index) return check_range(target, src, len, src->data + src_idx + c - 0xb7 + 1, RLP_ITEM);
      src_idx += len + c - 0xb7;
    }
    else if (c < 0xf8) {
      size_t len = c - 0xc0;
      if ((int) pos == index)
        return check_range(target, src, len, src->data + src_idx + 1, RLP_LIST);
      src_idx += len;
    }
    else {
      size_t len = 0;
      for (size_t i; i < (uint8_t) (c - 0xF7); i++)
        len |= (*(src->data + src_idx + 1 + i)) << (8 * ((c - 0xf7) - i - 1));
      if ((int) pos == index)
        return check_range(target, src, len, src->data + src_idx + c - 0xf7 + 1, RLP_LIST);
      src_idx += len + c - 0xf7;
    }
  }

  if (index < 0) return src_idx == src->len ? (rlp_type_t) pos : RLP_OUT_OF_RANGE;

  return (src_idx > src->len) ? RLP_OUT_OF_RANGE : RLP_NOT_FOUND;
}