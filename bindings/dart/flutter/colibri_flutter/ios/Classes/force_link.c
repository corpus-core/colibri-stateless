// Force the linker to retain all Colibri C symbols from the static XCFramework.
// Without explicit references the linker dead-strips them because they are only
// called via dlsym at runtime (Dart FFI).

extern void* c4_create_prover_ctx();
extern int   c4_prover_execute_json_status();
extern void* c4_prover_get_proof();
extern void  c4_free_prover_ctx();
extern void  c4_req_set_response();
extern void  c4_req_set_error();
extern void* c4_verify_create_ctx();
extern int   c4_verify_execute_json_status();
extern void  c4_verify_free_ctx();
extern int   c4_get_method_support();
extern void  c4_set_storage_config();
extern void  buffer_append();
extern void* c4_create_rpc_ctx();
extern int   c4_rpc_execute_json_status();
extern void  c4_free_rpc_ctx();
extern void  c4_set_checkpoint();
extern void  c4_rpc_set_witness_keys();

__attribute__((used))
void _colibri_force_link(void) {
    volatile void* syms[] = {
        (void*)c4_create_prover_ctx,
        (void*)c4_prover_execute_json_status,
        (void*)c4_prover_get_proof,
        (void*)c4_free_prover_ctx,
        (void*)c4_req_set_response,
        (void*)c4_req_set_error,
        (void*)c4_verify_create_ctx,
        (void*)c4_verify_execute_json_status,
        (void*)c4_verify_free_ctx,
        (void*)c4_get_method_support,
        (void*)c4_set_storage_config,
        (void*)buffer_append,
        (void*)c4_create_rpc_ctx,
        (void*)c4_rpc_execute_json_status,
        (void*)c4_free_rpc_ctx,
        (void*)c4_set_checkpoint,
        (void*)c4_rpc_set_witness_keys,
    };
    (void)syms;
}
