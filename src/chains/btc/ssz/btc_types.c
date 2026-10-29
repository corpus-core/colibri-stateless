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

#include "btc_types.h"

// Index 0 is frozen as NONE. Proof index 1 is the header witness. Sync-data index 1 is the epoch path.
static const ssz_def_t BTC_HEADER80            = SSZ_BYTE_VECTOR("header", 80);
static const ssz_def_t BTC_SIBLING32           = SSZ_BYTES32("sibling");
static const ssz_def_t BTC_EPOCH_STEP_FIELDS[] = {
    SSZ_UINT32("height"),
    SSZ_BYTE_VECTOR("header", 80),
    SSZ_LIST("final_headers", BTC_HEADER80, 64),
};
static const ssz_def_t BTC_EPOCH_STEP = SSZ_CONTAINER("BtcEpochStep", BTC_EPOCH_STEP_FIELDS);

// : Bitcoin
//
// Bitcoin proofs verify mainnet block headers and the values taken from them.
// A header is accepted after proof-of-work, the cached difficulty target, and at
// least six following headers. Heights below BIP34 are rejected.

// :: Proofs

// ::: Header Proof
//
// `BtcHeaderProof` is proof-union index 1. It carries the block height, the
// 80-byte header, up to 64 following headers, the transaction count, the coinbase
// merkle siblings, and the coinbase transaction. The coinbase merkle proof binds
// the height, except for the compiled checkpoint header, whose hash already fixes
// that height. `getblockheader` returns the raw header. `getblockhash` returns
// the display-order hash.
static const ssz_def_t BTC_HEADER_PROOF[] = {
    SSZ_UINT32("height"),
    SSZ_BYTE_VECTOR("header", 80),
    SSZ_LIST("final_headers", BTC_HEADER80, 64),
    SSZ_UINT32("tx_count"),
    SSZ_LIST("merkle", BTC_SIBLING32, 32),
    SSZ_BYTES("coinbase", 1000000),
};

// ::: Epoch Path
//
// `BtcEpochPath` is sync-data index 1. It carries up to 40 steps. Each step has
// a height, an 80-byte header, and up to 64 trailing finality headers. The path
// brings the cached epoch targets forward before the block proof is checked.
static const ssz_def_t BTC_EPOCH_PATH[] = {
    SSZ_LIST("steps", BTC_EPOCH_STEP, 40),
};

// ::: Request
//
// `C4Request` holds `version`, `data`, `proof`, and `sync_data`. Union index 0
// of each union is `NONE`. `data` index 1 is `BtcHeader`, the raw 80-byte header.
// `data` index 2 is `BtcBlockHash`, the 32-byte display-order hash. Further
// variants are appended; existing indices stay frozen.
static const ssz_def_t BTC_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTE_VECTOR("BtcHeader", 80),
    SSZ_BYTES32("BtcBlockHash"),
};

static const ssz_def_t BTC_REQUEST_PROOF_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BtcHeaderProof", BTC_HEADER_PROOF),
};

static const ssz_def_t BTC_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BtcEpochPath", BTC_EPOCH_PATH),
};

static const ssz_def_t BTC_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_UNION("data", BTC_REQUEST_DATA_UNION),
    SSZ_UNION("proof", BTC_REQUEST_PROOF_UNION),
    SSZ_UNION("sync_data", BTC_REQUEST_SYNCDATA_UNION),
};

static const ssz_def_t BTC_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", BTC_REQUEST);

/**
 * Returns the union member whose child-definition pointer is `fields`.
 *
 * Container, list, vector, and union definitions store that pointer in the first field of `def`.
 * A missing match returns NULL instead of the `NONE` slot.
 *
 * @param array union members
 * @param len number of members in `array`
 * @param fields child definition, such as a container's field list
 * @return the matching member, or NULL
 */
static const ssz_def_t* btc_union_variant(const ssz_def_t* array, size_t len, const ssz_def_t* fields) {
  size_t i;
  if (!array || !fields) return NULL;
  for (i = 0; i < len; i++) {
    if (array[i].type >= SSZ_TYPE_CONTAINER && array[i].def.container.elements == fields) return array + i;
  }
  return NULL;
}

#define BTC_UNION_VARIANT(array, fields) btc_union_variant((array), sizeof(array) / sizeof(ssz_def_t), (fields))

const ssz_def_t* btc_ssz_verification_type(btc_ssz_type_t type) {
  switch (type) {
    case BTC_SSZ_VERIFY_REQUEST:
      return &BTC_REQUEST_CONTAINER;
    case BTC_SSZ_VERIFY_HEADER_PROOF:
      return BTC_UNION_VARIANT(BTC_REQUEST_PROOF_UNION, BTC_HEADER_PROOF);
    case BTC_SSZ_DATA_HEADER:
      return BTC_REQUEST_DATA_UNION + 1;
    case BTC_SSZ_DATA_BLOCK_HASH:
      return BTC_REQUEST_DATA_UNION + 2;
    case BTC_SSZ_VERIFY_EPOCH_PATH:
      return BTC_UNION_VARIANT(BTC_REQUEST_SYNCDATA_UNION, BTC_EPOCH_PATH);
    default:
      return NULL;
  }
}
