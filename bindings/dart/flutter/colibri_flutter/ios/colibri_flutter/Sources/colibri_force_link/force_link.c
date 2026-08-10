// Force the linker to retain Colibri C symbols from the static XCFramework.
//
// Dart FFI resolves these via dlsym on DynamicLibrary.process() on iOS. With
// Flutter's `use_frameworks!`, CocoaPods builds this plugin as a dynamic
// framework and links the static c4_swift archive into it. Without live
// references, ld dead-strips every c4_* symbol and FFI fails at runtime with
// "Failed to lookup symbol (c4_create_prover_ctx)".
//
// A function-local `volatile void*[]` is not enough: Clang can empty
// `_colibri_force_link` under optimization, leaving no undefined refs and an
// empty framework (~100 KB instead of ~2 MB). File-scope `__attribute__((used))`
// pointers keep real relocations that survive compile- and link-time DCE.

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
COLIBRI_KEEP(c4_rpc_set_witness_keys);
// Required by Dart FFI (native.dart). Without this keeper, Release device
// builds dead-strip the symbol while Debug/simulator often still keep it.
COLIBRI_KEEP(c4_rpc_set_proxy_urls);
COLIBRI_KEEP(c4_rpc_set_min_latest_block_ts);
COLIBRI_KEEP(c4_verify_set_min_latest_block_ts);

// Extra entry point so tooling/tests can confirm the object was linked.
__attribute__((used, visibility("default"))) void _colibri_force_link(void) {
  // Touch one keeper so the translation unit cannot be treated as empty.
  (void)_colibri_keep_c4_create_prover_ctx;
}
