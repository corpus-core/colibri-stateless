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

#ifndef rpc_body_h__
#define rpc_body_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include <stdbool.h>

/**
 * Rewrites an `eth_createAccessList` JSON-RPC body for Nethermind.
 *
 * Nethermind defaults `optimize` to true and then drops the already-warm
 * transaction recipient, including every storage key read on that account.
 * The fourth parameter `false` keeps those keys. Other clients reject the
 * extra parameter (`-32602` / `-32601`), so the body is returned unchanged
 * unless `nethermind` is true.
 *
 * A body that already has four `params` entries is left unchanged. With two
 * entries the rewrite inserts `null, false` (no state override). With three
 * entries it appends `false` after the existing override.
 *
 * When the body is rewritten, the new bytes are stored in `out` and the
 * caller frees that buffer. Otherwise `out` is left untouched and the
 * returned view aliases `payload`.
 *
 * @param payload JSON-RPC request body
 * @param nethermind true when the selected execution client is Nethermind
 * @param out destination buffer for a rewritten body; must be zeroed
 * @return the body to send
 */
bytes_t c4_nethermind_access_list_body(bytes_t payload, bool nethermind, buffer_t* out);

#ifdef __cplusplus
}
#endif

#endif // rpc_body_h__
