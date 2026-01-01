use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RequestType {
    BeaconApi,
    #[serde(alias = "eth_rpc")]
    JsonRpc,
    Checkpointz,
}

impl Default for RequestType {
    fn default() -> Self {
        RequestType::JsonRpc
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum HttpMethod {
    Get,
    Post,
}

impl Default for HttpMethod {
    fn default() -> Self {
        HttpMethod::Get
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Encoding {
    Json,
    Ssz,
}

impl Default for Encoding {
    fn default() -> Self {
        Encoding::Json
    }
}
