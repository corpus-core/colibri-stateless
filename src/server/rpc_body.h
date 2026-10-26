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

/**
 * Applies client-specific patches to a JSON-RPC request body.
 *
 * Nethermind (`RPC_CLIENT_NETHERMIND`) defaults `eth_createAccessList`
 * `optimize` to true and then drops the already-warm transaction recipient,
 * including every storage key read on that account. The fourth parameter
 * `false` keeps those keys. Other clients reject the extra parameter
 * (`-32602` / `-32601`), so the body is returned unchanged unless that bit
 * is set.
 *
 * A body that already has four `params` entries is left unchanged. With two
 * entries the rewrite inserts `null, false` (no state override). With three
 * entries it appends `false` after the existing override.
 *
 * The prover server is the only caller. It passes the client-type bitmask of
 * the selected execution URL. CLI and language bindings send the original
 * body, because those hosts do not know which client will answer. Further
 * client patches belong in this function.
 *
 * When the body is rewritten, the new bytes are stored in `out` and the
 * caller frees that buffer. Otherwise `out` is left untouched and the
 * returned view aliases `payload`.
 *
 * @param payload JSON-RPC request body
 * @param client_type client-type bitmask of the selected server, or 0
 * @param out destination buffer for a rewritten body; must be zeroed
 * @return the body to send
 */
bytes_t c4_rpc_request_body(bytes_t payload, uint32_t client_type, buffer_t* out);

#ifdef __cplusplus
}
#endif

#endif // rpc_body_h__
