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
static const ssz_def_t BTC_EPOCH_STEP          = SSZ_CONTAINER("BtcEpochStep", BTC_EPOCH_STEP_FIELDS);
static const ssz_def_t BTC_EPOCH_PATH_FIELDS[] = {
    SSZ_LIST("steps", BTC_EPOCH_STEP, 40),
};
static const ssz_def_t BTC_HEADER_PROOF_FIELDS[] = {
    SSZ_UINT32("height"),
    SSZ_BYTE_VECTOR("header", 80),
    SSZ_LIST("final_headers", BTC_HEADER80, 64),
    SSZ_UINT32("tx_count"),
    SSZ_LIST("merkle", BTC_SIBLING32, 32),
    SSZ_BYTES("coinbase", 1000000),
};
static const ssz_def_t BTC_HEADER_PROOF = SSZ_CONTAINER("BtcHeaderProof", BTC_HEADER_PROOF_FIELDS);

static const ssz_def_t BTC_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTE_VECTOR("BtcHeader", 80),
};

static const ssz_def_t BTC_REQUEST_PROOF_UNION[] = {
    SSZ_NONE,
    BTC_HEADER_PROOF,
};

static const ssz_def_t BTC_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BtcEpochPath", BTC_EPOCH_PATH_FIELDS),
};

static const ssz_def_t BTC_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_UNION("data", BTC_REQUEST_DATA_UNION),
    SSZ_UNION("proof", BTC_REQUEST_PROOF_UNION),
    SSZ_UNION("sync_data", BTC_REQUEST_SYNCDATA_UNION),
};

static const ssz_def_t BTC_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", BTC_REQUEST);

const ssz_def_t* btc_ssz_verification_type(btc_ssz_type_t type) {
  switch (type) {
    case BTC_SSZ_VERIFY_REQUEST:
      return &BTC_REQUEST_CONTAINER;
    default:
      return NULL;
  }
}
