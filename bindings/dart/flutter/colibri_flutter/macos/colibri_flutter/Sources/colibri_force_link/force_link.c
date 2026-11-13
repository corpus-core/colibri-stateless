// Retain Colibri C symbols when the macOS app links via Swift Package Manager.
//
// Dart FFI resolves these with DynamicLibrary.open on the bundled dylib. The
// shared library is built with bindings/dart/force_link.c, but this target keeps
// the XCFramework linked into the Flutter macOS runner when SPM is enabled.

#include <stddef.h>
#include <stdint.h>

extern void*              c4_create_prover_ctx(void);
extern int                c4_prover_execute_json_status(void);
extern void*              c4_prover_get_proof(void);
extern void               c4_free_prover_ctx(void);
extern void               c4_req_set_response(void);
extern void               c4_req_set_error(void);
extern void*              c4_verify_create_ctx(void);
extern int                c4_verify_execute_json_status(void);
extern void               c4_verify_free_ctx(void);
extern int                c4_get_method_support(void);
extern unsigned long long c4_get_l1_chain_id_for_l2(unsigned long long);
extern void               c4_set_storage_config(void);
extern void               buffer_append(void);
extern void*              c4_create_rpc_ctx(void);
extern int                c4_rpc_execute_json_status(void);
extern void               c4_free_rpc_ctx(void);
extern void               c4_set_checkpoint(void);
extern void               c4_reset_caches(void);
extern void               c4_rpc_set_witness_keys(void);
extern void               c4_rpc_set_proxy_urls(void);
extern void               c4_rpc_set_min_latest_block_ts(void);
extern void               c4_verify_set_min_latest_block_ts(void);

#define COLIBRI_KEEP(sym) \
  __attribute__((used)) static void* const _colibri_keep_##sym = (void*) (uintptr_t) &sym

// Code relocation from an exported function. A __DATA_CONST pointer alone is
// not a live root for every Apple linker, so dlsym can miss c4_reset_caches.
#define COLIBRI_USE(sym) __asm__ volatile("" : : "r"((uintptr_t) &(sym)))

COLIBRI_KEEP(c4_create_prover_ctx);
COLIBRI_KEEP(c4_prover_execute_json_status);
COLIBRI_KEEP(c4_prover_get_proof);
COLIBRI_KEEP(c4_free_prover_ctx);
COLIBRI_KEEP(c4_req_set_response);
COLIBRI_KEEP(c4_req_set_error);
COLIBRI_KEEP(c4_verify_create_ctx);
COLIBRI_KEEP(c4_verify_execute_json_status);
COLIBRI_KEEP(c4_verify_free_ctx);
COLIBRI_KEEP(c4_get_method_support);
COLIBRI_KEEP(c4_get_l1_chain_id_for_l2);
COLIBRI_KEEP(c4_set_storage_config);
COLIBRI_KEEP(buffer_append);
COLIBRI_KEEP(c4_create_rpc_ctx);
COLIBRI_KEEP(c4_rpc_execute_json_status);
COLIBRI_KEEP(c4_free_rpc_ctx);
COLIBRI_KEEP(c4_set_checkpoint);
COLIBRI_KEEP(c4_reset_caches);
COLIBRI_KEEP(c4_rpc_set_witness_keys);
COLIBRI_KEEP(c4_rpc_set_proxy_urls);
COLIBRI_KEEP(c4_rpc_set_min_latest_block_ts);
COLIBRI_KEEP(c4_verify_set_min_latest_block_ts);

__attribute__((used, visibility("default"))) void _colibri_macos_force_link(void) {
  COLIBRI_USE(c4_create_prover_ctx);
  COLIBRI_USE(c4_prover_execute_json_status);
  COLIBRI_USE(c4_prover_get_proof);
  COLIBRI_USE(c4_free_prover_ctx);
  COLIBRI_USE(c4_req_set_response);
  COLIBRI_USE(c4_req_set_error);
  COLIBRI_USE(c4_verify_create_ctx);
  COLIBRI_USE(c4_verify_execute_json_status);
  COLIBRI_USE(c4_verify_free_ctx);
  COLIBRI_USE(c4_get_method_support);
  COLIBRI_USE(c4_get_l1_chain_id_for_l2);
  COLIBRI_USE(c4_set_storage_config);
  COLIBRI_USE(buffer_append);
  COLIBRI_USE(c4_create_rpc_ctx);
  COLIBRI_USE(c4_rpc_execute_json_status);
  COLIBRI_USE(c4_free_rpc_ctx);
  COLIBRI_USE(c4_set_checkpoint);
  COLIBRI_USE(c4_reset_caches);
  COLIBRI_USE(c4_rpc_set_witness_keys);
  COLIBRI_USE(c4_rpc_set_proxy_urls);
  COLIBRI_USE(c4_rpc_set_min_latest_block_ts);
  COLIBRI_USE(c4_verify_set_min_latest_block_ts);
}
