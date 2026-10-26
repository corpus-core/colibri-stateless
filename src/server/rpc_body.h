/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
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
 * The prover server is the only caller. It sets `nethermind` from the client
 * type of the selected execution URL. CLI and language bindings send the
 * original body, because those hosts do not know which client will answer.
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
