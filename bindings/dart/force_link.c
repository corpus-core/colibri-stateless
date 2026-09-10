// Keep Dart FFI entry points visible in libcolibri shared libraries.
//
// macOS/Linux/Android link with dead stripping; symbols only referenced via
// dlsym from Dart are removed unless this translation unit holds relocations.

#include <stddef.h>
#include <stdint.h>

extern void* c4_create_prover_ctx(void);
extern int c4_prover_execute_json_status(void);
extern void* c4_prover_get_proof(void);
extern void c4_free_prover_ctx(void);
extern void c4_req_set_response(void);
extern void c4_req_set_error(void);
extern void* c4_verify_create_ctx(void);
extern int c4_verify_execute_json_status(void);
extern void c4_verify_free_ctx(void);
extern int c4_get_method_support(void);
extern void c4_set_storage_config(void);
extern void buffer_append(void);
extern void* c4_create_rpc_ctx(void);
extern int c4_rpc_execute_json_status(void);
extern void c4_free_rpc_ctx(void);
extern void c4_set_checkpoint(void);
extern void c4_reset_caches(void);
extern void c4_rpc_set_witness_keys(void);
extern void c4_rpc_set_proxy_urls(void);
extern void c4_rpc_set_min_latest_block_ts(void);
extern void c4_verify_set_min_latest_block_ts(void);

#define COLIBRI_KEEP(sym) \
  __attribute__((used)) static void* const _colibri_keep_##sym = (void*)(uintptr_t)&sym

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

__attribute__((used, visibility("default"))) void colibri_dart_force_link(void) {
  (void)_colibri_keep_c4_reset_caches;
}
