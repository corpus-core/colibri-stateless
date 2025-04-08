#include "rlp.h"

static int check_range(bytes_t* target, bytes_t* src, size_t new_len, uint8_t* new_start, rlp_type_t result_type) {
  if (!target) return RLP_OUT_OF_RANGE;
  *target = bytes(new_start, new_len);
  return (new_start >= src->data && (new_start + new_len) >= src->data && (new_start + new_len) <= (src->data + src->len)) ? result_type : RLP_OUT_OF_RANGE;
}

INTERNAL rlp_type_t rlp_decode(bytes_t* src, int index, bytes_t* target) {
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
      for (size_t i = 0; i < (uint8_t) (c - 0xF7); i++)
        len |= (*(src->data + src_idx + 1 + i)) << (8 * ((c - 0xf7) - i - 1));
      if ((int) pos == index)
        return check_range(target, src, len, src->data + src_idx + c - 0xf7 + 1, RLP_LIST);
      src_idx += len + c - 0xf7;
    }
  }

  if (index < 0) return src_idx == src->len ? (rlp_type_t) pos : RLP_OUT_OF_RANGE;

  return (src_idx > src->len) ? RLP_OUT_OF_RANGE : RLP_NOT_FOUND;
}

static void encode_length(buffer_t* buf, uint32_t len, uint8_t offset) {
  uint8_t val = offset;
  if (len < 56)
    buffer_add_bytes(buf, 1, offset + len);
  else if (len < 0x100)
    buffer_add_bytes(buf, 2, offset + 55 + 1, len);
  else if (len < 0x10000) {
    buffer_add_bytes(buf, 1, offset + 55 + 2);
    buffer_add_be(buf, len, 2);
  }
  else if (len < 0x1000000) {
    buffer_add_bytes(buf, 1, offset + 55 + 3);
    buffer_add_be(buf, len, 3);
  }
  else {
    buffer_add_bytes(buf, 1, offset + 55 + 4);
    buffer_add_be(buf, len, 4);
  }
}

INTERNAL void rlp_add_item(buffer_t* buf, bytes_t data) {
  if (data.len == 1 && data.data[0] < 0x80) {
  }
  else if (data.len < 56)
    buffer_add_bytes(buf, 1, data.len + 0x80);
  else
    encode_length(buf, data.len, 0x80);
  buffer_append(buf, data);
}

INTERNAL void rlp_add_list(buffer_t* buf, bytes_t data) {
  encode_length(buf, data.len, 0xc0);
  buffer_append(buf, data);
}

INTERNAL void rlp_add_uint(buffer_t* buf, bytes_t data) {
  while (data.len && data.data[0] == 0) {
    data.data++;
    data.len--;
  }
  rlp_add_item(buf, data);
}

INTERNAL void rlp_add_uint64(buffer_t* buf, uint64_t value) {
  uint8_t data[8] = {0};
  uint64_to_be(data, value);
  rlp_add_uint(buf, bytes(data, 8));
}

INTERNAL void rlp_to_list(buffer_t* buf) {
  uint8_t  tmp[4] = {0};
  buffer_t tbuf   = stack_buffer(tmp);
  encode_length(&tbuf, buf->data.len, 0xc0);
  buffer_splice(buf, 0, 0, tbuf.data);
}

INTERNAL uint64_t rlp_get_uint64(bytes_t data, int index) {
  uint64_t value = 0;
  rlp_decode(&data, index, &data);
  if (data.len > 8 || !data.len) return 0;
  for (int i = 0; i < data.len; i++)
    value |= ((uint64_t) data.data[i]) << ((data.len - i - 1) << 3);
  return value;
}