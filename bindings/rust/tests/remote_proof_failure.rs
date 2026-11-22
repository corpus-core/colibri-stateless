//! What the RPC context does when the remote prover fails, with and
//! without the `prover` feature (a `PROVER=OFF` C core). No fixtures or
//! network: the test answers the context's requests itself.

use colibri_stateless::{
    register_storage, req_set_error, MemoryStorage, ProverMode, RequestType, RpcCtx, Status,
};
use serial_test::serial;

fn step(ctx: &mut RpcCtx) -> Status {
    Status::parse(&ctx.execute_json_status().expect("step")).expect("status JSON")
}

#[test]
#[serial]
fn failed_remote_proof() {
    register_storage(MemoryStorage::new());
    let mut ctx =
        RpcCtx::new("eth_blockNumber", "[]", 1, 0, 0, ProverMode::Remote).expect("rpc ctx");

    let Status::Pending { requests } = step(&mut ctx) else {
        panic!("expected the remote proof request");
    };
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].request_type, RequestType::Prover);
    // SAFETY: the request comes from the latest step of `ctx`, which is
    // alive and has not been advanced since.
    unsafe { req_set_error(requests[0].req_ptr, "prover down", 0) }.expect("set error");

    match step(&mut ctx) {
        // Verifier-only: the context ends instead of proving locally.
        #[cfg(not(feature = "prover"))]
        Status::Error { message } => {
            assert_eq!(
                message,
                "remote proof failed (no local proving): prover down"
            )
        }
        // With the prover: it falls back to a local proof and fetches
        // the data for it itself.
        #[cfg(feature = "prover")]
        Status::Pending { requests } => assert!(
            requests
                .iter()
                .all(|r| r.request_type != RequestType::Prover),
            "local proving fetches its own data: {requests:?}"
        ),
        other => panic!("unexpected step: {other:?}"),
    }
}

#[cfg(not(feature = "prover"))]
#[test]
#[serial]
fn local_proving_modes_are_refused() {
    register_storage(MemoryStorage::new());
    for mode in [
        ProverMode::Local,
        ProverMode::Hybrid,
        ProverMode::LightClient,
    ] {
        let mut ctx = RpcCtx::new("eth_blockNumber", "[]", 1, 0, 0, mode).expect("rpc ctx");
        match step(&mut ctx) {
            Status::Error { message } => assert!(
                message.contains("PROVER=OFF"),
                "{mode:?}: unexpected message: {message}"
            ),
            other => panic!("{mode:?}: expected an error, got {other:?}"),
        }
    }
}
