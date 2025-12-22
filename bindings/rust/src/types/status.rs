use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Deserialize)]
#[serde(tag = "status")]
pub enum Status {
    #[serde(rename = "pending")]
    Pending { requests: Vec<HttpRequest> },

    #[serde(rename = "error")]
    Error { message: String },

    #[serde(rename = "done")]
    Done,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct HttpRequest {
    pub url: String,
    pub method: String,
    pub headers: HashMap<String, String>,
    pub body: Option<String>,
    pub request_id: usize,
    pub node_index: u16,
}