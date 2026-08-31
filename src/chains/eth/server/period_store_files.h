/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef C4_ETH_PERIOD_STORE_FILES_H
#define C4_ETH_PERIOD_STORE_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Request prefixes and artifact filenames under `<period>/`.
 *
 * Included from `period_store.h`. The prover includes this file directly so it
 * can build internal request URLs without pulling in `server.h`.
 *
 * `C4_PS_INTERNAL_PREFIX` and `C4_PS_HTTP_PREFIX` share the `period_store` path
 * segment, but they are different protocols: the prover talks to the local
 * server via `c4_send_internal_request`, while a slave fetches from a master
 * over HTTP (`c4_handle_period_static`).
 */
#define C4_PS_INTERNAL_PREFIX "period_store/" /**< prover / `c4_handle_period_store` */
#define C4_PS_HTTP_PREFIX     "/period_store" /**< slave→master HTTP (`c4_handle_period_static`) */

/* Beacon / light-client artifacts */
#define C4_PS_SYNC_SSZ             "sync.ssz"             /**< prover input for the period ZK proof */
#define C4_PS_LCB_SSZ              "lcb.ssz"              /**< LightClientBootstrap */
#define C4_PS_LCU_SSZ              "lcu.ssz"              /**< LightClientUpdate */
#define C4_PS_HISTORICAL_ROOT_JSON "historical_root.json" /**< historical_summaries + proof (cached Beacon call) */
#define C4_PS_HEADERS_SSZ          "headers.ssz"          /**< 8192 SSZ-serialized headers */
#define C4_PS_BLOCKS_SSZ           "blocks.ssz"           /**< 8192 block roots */
#define C4_PS_BLOCKS_ROOT_BIN      "blocks_root.bin"      /**< hash_tree_root of `blocks.ssz`; presence means verified */

/* ZK artifacts — same names as `scripts/run_zk_proof.sh`. No `_v6` suffix. */
#define C4_PS_ZK_GROTH16   "zk_groth16.bin"   /**< SP1 Groth16 proof bundle */
#define C4_PS_ZK_PROOF_G16 "zk_proof_g16.bin" /**< raw Groth16 proof (packed into ssz + verified locally) */
#define C4_PS_ZK_VK        "zk_vk.bin"        /**< Groth16 verification key */
#define C4_PS_ZK_PUB       "zk_pub.bin"       /**< public values (verified locally) */
#define C4_PS_ZK_PROOF     "zk_proof.bin"     /**< compressed proof (recursion input for next period) */
#define C4_PS_ZK_VK_RAW    "zk_vk_raw.bin"    /**< compressed vk (recursion input for next period) */
#define C4_PS_ZK_PROOF_SSZ "zk_proof.ssz"     /**< packed `ZKSyncDataV6` for the verifier */

#ifdef __cplusplus
}
#endif

#endif /* C4_ETH_PERIOD_STORE_FILES_H */
