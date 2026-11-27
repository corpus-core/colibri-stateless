//! Driving an [`RpcCtx`] by hand through the safe answer API
//! ([`Step`] / [`PendingRequest`]), without fixtures or the network.
//!
//! `eth_gasPrice` is unproofable: the context asks the host for one
//! JSON-RPC call and returns its result, so a test can answer that
//! request directly.

use colibri_stateless::{PendingRequest, ProverMode, RequestType, RpcCtx, Step};
use serde_json::json;
use serial_test::serial;

fn gas_price_ctx() -> RpcCtx {
    RpcCtx::new("eth_gasPrice", "[]", 1, 0, 0, ProverMode::Remote).expect("rpc ctx")
}

fn single_request<'a>(step: Step<'a>) -> PendingRequest<'a> {
    match step {
        Step::Pending { mut requests } => {
            assert_eq!(requests.len(), 1, "one pending request: {requests:?}");
            requests.pop().unwrap()
        }
        other => panic!("expected a pending step, got {other:?}"),
    }
}

#[test]
#[serial]
fn response_answers_the_request() {
    let mut ctx = gas_price_ctx();
    let request = single_request(ctx.execute().unwrap());
    assert_eq!(request.request_type, RequestType::EthRpc);
    assert_eq!(request.chain_id, 1);
    let payload = request.payload.as_ref().expect("JSON-RPC payload");
    assert_eq!(payload["method"], "eth_gasPrice");

    request.set_response(br#"{"jsonrpc":"2.0","id":1,"result":"0x3b9aca00"}"#, 0);

    match ctx.execute().unwrap() {
        Step::Success { result } => assert_eq!(result, Some(json!("0x3b9aca00"))),
        other => panic!("expected success, got {other:?}"),
    }
}

#[test]
#[serial]
fn error_answers_the_request() {
    let mut ctx = gas_price_ctx();
    single_request(ctx.execute().unwrap()).set_error("upstream down", 0);

    match ctx.execute().unwrap() {
        Step::Error { message } => assert!(
            message.contains("upstream down"),
            "unexpected message: {message}"
        ),
        other => panic!("expected an error, got {other:?}"),
    }
}

#[test]
#[serial]
fn error_message_is_cut_at_a_nul_byte() {
    let mut ctx = gas_price_ctx();
    single_request(ctx.execute().unwrap()).set_error("bad\0tail", 0);

    match ctx.execute().unwrap() {
        Step::Error { message } => {
            assert!(message.contains("bad"), "unexpected message: {message}");
            assert!(!message.contains("tail"), "unexpected message: {message}");
        }
        other => panic!("expected an error, got {other:?}"),
    }
}

#[test]
#[serial]
fn unanswered_request_stays_pending() {
    let mut ctx = gas_price_ctx();
    let first = single_request(ctx.execute().unwrap()).req_ptr;
    // Dropped without an answer: the next step asks for it again.
    let again = single_request(ctx.execute().unwrap());
    assert_eq!(again.req_ptr, first);
    again.set_response(br#"{"jsonrpc":"2.0","id":1,"result":"0x1"}"#, 0);
    assert!(matches!(ctx.execute().unwrap(), Step::Success { .. }));
}

#[test]
#[serial]
fn status_text_comes_with_its_step() {
    let mut ctx = gas_price_ctx();
    let (step, status) = ctx.execute_with_status().unwrap();
    let request = single_request(step);
    let parsed: serde_json::Value = serde_json::from_str(&status).expect("JSON status");
    assert_eq!(parsed["status"], "pending");
    assert_eq!(parsed["requests"].as_array().map(Vec::len), Some(1));
    // The text names the request by the pointer the step's value carries.
    let pointer = &parsed["requests"][0]["req_ptr"];
    let pointer = pointer
        .as_u64()
        .or_else(|| pointer.as_str().and_then(|s| s.parse().ok()))
        .expect("req_ptr");
    assert_eq!(pointer, request.req_ptr);
    request.set_response(br#"{"jsonrpc":"2.0","id":1,"result":"0x2"}"#, 0);

    let (step, status) = ctx.execute_with_status().unwrap();
    assert!(matches!(step, Step::Success { result: Some(ref r) } if r == &json!("0x2")));
    let parsed: serde_json::Value = serde_json::from_str(&status).expect("JSON status");
    assert_eq!(parsed["status"], "success");
    assert_eq!(parsed["result"], json!("0x2"));
}
