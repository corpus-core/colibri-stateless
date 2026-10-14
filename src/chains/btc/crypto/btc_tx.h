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

#ifndef btc_tx_h__
#define btc_tx_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"

/**
 * Zero-copy view of one Bitcoin transaction.
 *
 * Every slice points into `raw` and stays valid only as long as that buffer does.
 * `vin` and `vout` include their compact-size counts. `witness` is the concatenation of the
 * per-input witness stacks and is empty unless `segwit` is true. The parser accepts legacy
 * transactions and BIP141 transactions whose witness flag is exactly 1. It rejects a non-canonical
 * compact size, trailing bytes, a transaction with no input or no output, and a witness flag whose
 * stacks are all empty.
 */
typedef struct {
  bytes_t  raw;          // full serialization the view was parsed from
  uint32_t version;      // raw little-endian version word, not a signed interpretation
  bool     segwit;       // true when the marker/flag and a non-empty witness are present
  uint32_t input_count;  // number of inputs
  uint32_t output_count; // number of outputs
  bytes_t  vin;          // compact-size count followed by the inputs
  bytes_t  vout;         // compact-size count followed by the outputs
  bytes_t  witness;      // witness stacks only; empty for a legacy transaction
  uint32_t locktime;     // raw little-endian locktime word
} btc_tx_t;

/**
 * Parses a legacy or BIP141 transaction.
 *
 * On failure `tx` is cleared. The function does not allocate and does not copy the transaction bytes.
 *
 * @param raw exact transaction encoding, with no trailing bytes
 * @param tx receives the view
 * @return true when `raw` is one complete transaction
 */
bool btc_tx_parse(bytes_t raw, btc_tx_t* tx) NONNULL_FOR((2));

/**
 * Writes the txid.
 *
 * The txid is HASH256 of the non-witness serialization (`version || vin || vout || locktime`).
 *
 * @param tx parsed transaction
 * @param out 32-byte internal-order txid
 * @return true when `out` was written
 */
bool btc_tx_id(const btc_tx_t* tx, uint8_t out[32]) NONNULL;

/**
 * Writes the wtxid.
 *
 * A legacy transaction has `wtxid == txid`. A segwit transaction hashes the full serialization,
 * including the marker, flag, and witness.
 *
 * @param tx parsed transaction
 * @param out 32-byte internal-order wtxid
 * @return true when `out` was written
 */
bool btc_tx_wtxid(const btc_tx_t* tx, uint8_t out[32]) NONNULL;

/**
 * Reads one input.
 *
 * Any out-parameter may be NULL. `prev_txid` and `script` point into `tx->raw`.
 *
 * @param tx parsed transaction
 * @param index input index, starting at 0
 * @param prev_txid receives the 32-byte internal-order previous txid
 * @param prev_index receives the previous output index
 * @param script receives the scriptSig
 * @param sequence receives the sequence word
 * @return true when the input exists and was decoded
 */
bool btc_tx_input(const btc_tx_t* tx, uint32_t index, bytes_t* prev_txid, uint32_t* prev_index, bytes_t* script, uint32_t* sequence);

/**
 * Reads one output.
 *
 * `script` points into `tx->raw`. Either out-parameter may be NULL.
 *
 * @param tx parsed transaction
 * @param index output index, starting at 0
 * @param value receives the satoshi amount
 * @param script receives the scriptPubKey
 * @return true when the output exists and was decoded
 */
bool btc_tx_output(const btc_tx_t* tx, uint32_t index, uint64_t* value, bytes_t* script);

/**
 * Reads one witness stack item.
 *
 * `item` points into `tx->raw`.
 *
 * @param tx parsed segwit transaction
 * @param input input index
 * @param item_index index inside that input's witness stack
 * @param out receives the item
 * @return true when the item exists
 */
bool btc_tx_witness_item(const btc_tx_t* tx, uint32_t input, uint32_t item_index, bytes_t* out);

/**
 * Reads the coinbase witness nonce.
 *
 * BIP141 requires the coinbase witness to be exactly one 32-byte stack item. That item is the
 * nonce mixed into the witness commitment. This function checks the shape only; it does not decide
 * whether the transaction is a coinbase.
 *
 * @param tx parsed segwit transaction
 * @param nonce receives the 32-byte nonce inside `tx->raw`
 * @return true when input 0 has exactly one 32-byte witness item
 */
bool btc_tx_coinbase_nonce(const btc_tx_t* tx, bytes_t* nonce);

/**
 * Decodes the BIP34 block height from the first item of a coinbase scriptSig.
 *
 * The item must be `OP_0`, `OP_1` .. `OP_16`, or a minimally encoded non-negative script number of
 * at most five bytes. A non-minimal push and a negative script number are rejected. Bytes after the
 * first item are ignored, so a coinbase message may follow the height. The genesis coinbase starts
 * with a 4-byte push of the genesis `nBits` and therefore decodes as 486604799; the caller compares
 * the result with the block height only once BIP34 applies.
 *
 * @param script coinbase scriptSig
 * @param height receives the decoded number
 * @return true when the first item is a legal non-negative script number that fits in uint32
 */
bool btc_bip34_height(bytes_t script, uint32_t* height) NONNULL_FOR((2));

/**
 * Finds the witness commitment inside a coinbase's outputs.
 *
 * The commitment is the 32 bytes that follow the prefix `OP_RETURN 0x24 0xaa21a9ed`. A script longer
 * than that prefix still matches, which is the same rule as Bitcoin Core. If several outputs match,
 * the last one wins.
 *
 * @param tx parsed transaction, normally the coinbase
 * @param commitment receives a 32-byte view into `tx->raw`
 * @return true when at least one commitment output was found
 */
bool btc_tx_witness_commitment(const btc_tx_t* tx, bytes_t* commitment);

/**
 * Checks a witness commitment against a witness-merkle root and the coinbase nonce.
 *
 * The expected commitment is HASH256(`witness_root || nonce`). In the witness merkle tree the
 * coinbase leaf is 32 zero bytes; this function does not build that tree.
 *
 * @param commitment 32-byte commitment from the coinbase scriptPubKey
 * @param witness_root merkle root of the wtxids, internal order
 * @param nonce 32-byte coinbase witness nonce
 * @return true when the commitment is the hash of that pair
 */
bool btc_witness_commitment_matches(bytes_t commitment, const uint8_t witness_root[32], const uint8_t nonce[32]);

#ifdef __cplusplus
}
#endif

#endif
