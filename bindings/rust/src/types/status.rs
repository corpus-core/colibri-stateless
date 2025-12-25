use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(tag = "status")]
pub enum Status {
    #[serde(rename = "pending")]
    Pending { requests: Vec<HttpRequest> },

    #[serde(rename = "error")]
    Error {
        #[serde(rename = "error")]
        message: String
    },

    #[serde(rename = "success")]
    Success,
}

#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
pub struct HttpRequest {
    #[serde(rename = "req_ptr")]
    pub request_id: usize,
    pub url: String,
    pub method: String,
    #[serde(rename = "type")]
    pub request_type: String,
    #[serde(default)]
    pub chain_id: u64,
    #[serde(default)]
    pub encoding: String,
    #[serde(default)]
    pub exclude_mask: String,
    #[serde(default)]
    pub headers: HashMap<String, String>,
    #[serde(default)]
    pub body: Option<String>,
    #[serde(default)]
    pub payload: Option<serde_json::Value>,
    #[serde(default)]
    pub node_index: u16,
}