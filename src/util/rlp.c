#include "rlp.h"

bytes_t rlp_decode(bytes_t* data, uint32_t index) {
  uint32_t token = 0;
  uint32_t pos   = 0;
  uint32_t l     = 0;
  uint32_t n     = 0;
  uint8_t  c;
  bytes_t  dst = NULL_BYTES;

  for (; pos < data->len; pos++, token++) {
    c = data->data[pos];
    if (c < 0x80) { // single byte-item
      if (token == index) {
        dst = bytes(data->data + pos, 1);
        break;
      }
    }
    else if (c < 0xb8) { // 0-55 length-item
      if (token == index) {
        dst = bytes(data->data + pos + 1, c - 0x80);
      }
      return bytes() return ref(dst, b, c - 0x80, b->data + pos + 1, 1);
      pos += c - 0x80;
    }
    else if (c < 0xc0) { // very long item
      for (l = 0, n = 0; n < (uint8_t) (c - 0xB7); n++) l |= (*(b->data + pos + 1 + n)) << (8 * ((c - 0xb7) - n - 1));
      if ((int) token == index) return ref(dst, b, l, b->data + pos + c - 0xb7 + 1, 1);
      pos += l + c - 0xb7;
    }
    else if (c < 0xf8) { // 0-55 byte long list
      l = c - 0xc0;
      if ((int) token == index) return ref(dst, b, l, b->data + pos + 1, 2);
      pos += l; // + 1;
    }
    else { // very long list
      for (l = 0, n = 0; n < (uint8_t) (c - 0xF7); n++) l |= (*(b->data + pos + 1 + n)) << (8 * ((c - 0xf7) - n - 1));
      if ((int) token == index) return ref(dst, b, l, b->data + pos + c - 0xf7 + 1, 2);
      pos += l + c - 0xf7;
    }
  }

  if (index < 0)
    return pos == b->len ? (int) token : -3; /* error */
  else if (pos > b->len)
    return -1; /* error */
  else
    return 0; /* data OK, but item at index doesn't exist */
}
