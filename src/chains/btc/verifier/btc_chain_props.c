/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "btc_verify.h"

bool c4_btc_chain_props(chain_id_t chain_id, chain_properties_t* props) {
  if (!props || c4_chain_type(chain_id) != C4_CHAIN_TYPE_BITCOIN) return false;

  props->block_time = 600000;
  props->chain_type = C4_CHAIN_TYPE_BITCOIN;
  props->id         = chain_id;
  props->flags      = 0;

  switch (c4_chain_specific_id(chain_id)) {
    case 0:
      props->chain_name = "Bitcoin Mainnet";
      break;
    case 1:
      props->chain_name = "Bitcoin Testnet";
      break;
    case 2:
      props->chain_name = "Bitcoin Devnet";
      break;
    default:
      props->chain_name = "Bitcoin";
      break;
  }
  return true;
}
