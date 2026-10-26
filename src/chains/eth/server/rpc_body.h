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
 * Ethereum hook for `c4_server_handlers_rewrite_body`.
 *
 * The generic server calls every chain's rewrite with the client-type bitmask
 * of the selected URL. This function handles Ethereum execution clients.
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
 * CLI and language bindings do not call this. They send the original body,
 * because those hosts do not know which client will answer. Further Ethereum
 * client patches belong in this function. Other chains register their own
 * rewrite with `add_server_handler(REWRITE_BODY_FUNC ...)`.
 *
 * When the body is rewritten, the new bytes replace `out` and the caller
 * frees that buffer. `payload` may already alias `out` when a previous chain
 * rewrote the body. Otherwise `out` is left untouched and the returned view
 * aliases `payload`.
 *
 * @param payload JSON-RPC request body
 * @param client_type client-type bitmask of the selected server, or 0
 * @param out destination buffer for a rewritten body
 * @return the body to send
 */
bytes_t eth_rewrite_rpc_body(bytes_t payload, uint32_t client_type, buffer_t* out);

#ifdef __cplusplus
}
#endif

#endif // rpc_body_h__
