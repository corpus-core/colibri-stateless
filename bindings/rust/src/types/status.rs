use super::request::{Encoding, HttpMethod, RequestType};
use serde::{Deserialize, Deserializer, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(tag = "status")]
pub enum Status {
    #[serde(rename = "pending")]
    Pending { requests: Vec<HttpRequest> },

    #[serde(rename = "error")]
    Error {
        #[serde(rename = "error")]
        message: String,
    },

    #[serde(rename = "success")]
    Success,
}

fn string_to_u32<'de, D: Deserializer<'de>>(d: D) -> Result<u32, D::Error> {
    let s = String::deserialize(d)?;
    s.parse().map_err(serde::de::Error::custom)
}

#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
pub struct HttpRequest {
    #[serde(rename = "req_ptr")]
    pub request_id: usize,
    pub url: String,
    #[serde(default)]
    pub method: HttpMethod,
    #[serde(rename = "type", default)]
    pub request_type: RequestType,
    #[serde(default)]
    pub chain_id: u64,
    #[serde(default)]
    pub encoding: Encoding,
    #[serde(default, deserialize_with = "string_to_u32")]
    pub exclude_mask: u32,
    #[serde(default)]
    pub headers: HashMap<String, String>,
    #[serde(default)]
    pub body: Option<String>,
    #[serde(default)]
    pub payload: Option<serde_json::Value>,
    #[serde(default)]
    pub node_index: u16,
}
