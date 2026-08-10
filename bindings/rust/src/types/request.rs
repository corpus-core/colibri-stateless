//! Wire types for the JSON status protocol emitted by
//! `c4_*_execute_json_status`.
//!
//! The C core prints request pointers as decimal strings and
//! `exclude_mask` as a numeric string (see
//! `bindings/colibri_common.c::c4i_add_data_request`); we deserialise
//! them into `u64` / `u32` via custom visitors.

use serde::de::{self, Deserializer};
use serde::{Deserialize, Serialize};
use std::fmt;

/// HTTP verb for a pending [`DataRequest`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
#[allow(missing_docs)]
pub enum HttpMethod {
    #[default]
    Get,
    Post,
    Put,
    Delete,
}

impl HttpMethod {
    /// Uppercase form for use as an HTTP verb.
    pub fn as_str(&self) -> &'static str {
        match self {
            HttpMethod::Get => "GET",
            HttpMethod::Post => "POST",
            HttpMethod::Put => "PUT",
            HttpMethod::Delete => "DELETE",
        }
    }
}

/// Payload encoding advertised to the RPC endpoint.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
#[allow(missing_docs)]
pub enum Encoding {
    #[default]
    Json,
    Ssz,
}

impl fmt::Display for Encoding {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Encoding::Json => f.write_str("json"),
            Encoding::Ssz => f.write_str("ssz"),
        }
    }
}

/// Kind of endpoint the request should be routed to. Matches
/// `data_request_type_t` in the C core.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum RequestType {
    /// Beacon-chain REST API (`/eth/v1/...`).
    BeaconApi,
    /// Execution-layer JSON-RPC.
    #[default]
    EthRpc,
    /// Colibri prover HTTP endpoint.
    Prover,
    /// Beacon checkpoint sync endpoint.
    Checkpointz,
    /// Generic REST API.
    RestApi,
    /// Internal / cache request -- should never reach the host, kept for
    /// forward compatibility.
    Intern,
    /// Cache snapshot pre-populated by the core.
    Cache,
}

/// A single pending data request. Deserialised from the JSON status
/// blob emitted by the C core.
#[derive(Debug, Clone, Deserialize)]
#[allow(missing_docs)]
pub struct DataRequest {
    /// Opaque pointer to the pending `data_request_t` in the C core.
    /// Sent back to the core via `c4_req_set_response` /
    /// `c4_req_set_error`.
    #[serde(rename = "req_ptr", deserialize_with = "deser_u64_from_str_or_num")]
    pub req_ptr: u64,

    /// Chain ID for the request (informational).
    #[serde(default)]
    pub chain_id: u64,

    /// Payload encoding requested by the core (JSON or SSZ).
    #[serde(default)]
    pub encoding: Encoding,

    /// Bitmask of node indices that should be skipped when picking a
    /// server (a bit `1 << i` marks endpoint `i` as excluded, e.g.
    /// after a previous failure).
    #[serde(default, deserialize_with = "deser_u32_from_str_or_num")]
    pub exclude_mask: u32,

    /// Optional delay in milliseconds before executing the request
    /// (used by oblivious-node retry backoff).
    #[serde(default)]
    pub delay: u32,

    /// Optional TTL hint in seconds (`Cache-Control: max-age=<ttl>`).
    #[serde(default)]
    pub ttl: u32,

    /// HTTP method.
    #[serde(default)]
    pub method: HttpMethod,

    /// URL path (may be empty for POST-only endpoints).
    #[serde(default)]
    pub url: String,

    /// Structured payload for JSON-RPC requests.
    #[serde(default)]
    pub payload: Option<serde_json::Value>,

    /// Endpoint kind.
    #[serde(rename = "type", default)]
    pub request_type: RequestType,
}

/// Result of one `execute_json_status` step.
#[derive(Debug, Clone)]
#[allow(missing_docs)]
pub enum Status {
    /// External data required -- the host must fetch every entry in
    /// `requests`, then set the response via [`crate::req_set_response`]
    /// or [`crate::req_set_error`] before calling execute again.
    Pending { requests: Vec<DataRequest> },
    /// Verifier / RPC finished successfully with the given result.
    /// `result` is `None` when the caller reads the proof through
    /// `c4_prover_get_proof` (prover flow -- the "result" field then
    /// carries the pointer, not the payload).
    Success { result: Option<serde_json::Value> },
    /// Verified `eth_call` result: the EVM ran to completion but
    /// reverted. `data` holds the raw revert-data as `0x`-prefixed
    /// hex.
    Revert { data: String },
    /// The core reports a fatal error.
    Error { message: String },
}

impl Status {
    /// Parse a JSON status blob emitted by the C core. The blob has one
    /// of three shapes; see
    /// `bindings/colibri_common.c::c4i_build_verifier_json_status`.
    pub fn parse(json: &str) -> Result<Self, serde_json::Error> {
        #[derive(Deserialize)]
        struct Raw {
            status: String,
            #[serde(default)]
            requests: Vec<DataRequest>,
            #[serde(default)]
            result: Option<serde_json::Value>,
            #[serde(default)]
            data: Option<serde_json::Value>,
            #[serde(default)]
            error: Option<String>,
        }
        let raw: Raw = serde_json::from_str(json)?;
        Ok(match raw.status.as_str() {
            "pending" => Status::Pending {
                requests: raw.requests,
            },
            "success" => Status::Success { result: raw.result },
            "revert" => Status::Revert {
                data: match raw.data {
                    Some(serde_json::Value::String(s)) => s,
                    Some(other) => other.to_string(),
                    None => "0x".to_string(),
                },
            },
            "error" => Status::Error {
                message: raw.error.unwrap_or_else(|| "unknown error".into()),
            },
            other => Status::Error {
                message: format!("unknown status: {other}"),
            },
        })
    }
}

fn deser_u64_from_str_or_num<'de, D: Deserializer<'de>>(d: D) -> Result<u64, D::Error> {
    struct V;
    impl<'de> de::Visitor<'de> for V {
        type Value = u64;
        fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("a u64 or a string containing a u64")
        }
        fn visit_u64<E: de::Error>(self, v: u64) -> Result<u64, E> {
            Ok(v)
        }
        fn visit_i64<E: de::Error>(self, v: i64) -> Result<u64, E> {
            if v < 0 {
                Err(E::custom("negative u64"))
            } else {
                Ok(v as u64)
            }
        }
        fn visit_str<E: de::Error>(self, v: &str) -> Result<u64, E> {
            let trimmed = v.trim();
            if let Some(hex) = trimmed
                .strip_prefix("0x")
                .or_else(|| trimmed.strip_prefix("0X"))
            {
                u64::from_str_radix(hex, 16).map_err(E::custom)
            } else {
                trimmed.parse::<u64>().map_err(E::custom)
            }
        }
    }
    d.deserialize_any(V)
}

fn deser_u32_from_str_or_num<'de, D: Deserializer<'de>>(d: D) -> Result<u32, D::Error> {
    let v: u64 = deser_u64_from_str_or_num(d)?;
    u32::try_from(v).map_err(de::Error::custom)
}
