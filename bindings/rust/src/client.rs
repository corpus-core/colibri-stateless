use crate::prover::Prover;
use crate::types::{Result, ColibriError, Status, HttpRequest};
use reqwest::Client;
use serde_json;
use std::collections::HashMap;

pub struct ProverClient {
    prover: Prover,
    http_client: Client,
}

impl ProverClient {
    pub fn new(method: &str, params: &str, chain_id: u64, flags: u32) -> Result<Self> {
        let prover = Prover::new(method, params, chain_id, flags)?;
        let http_client = Client::new();

        Ok(Self {
            prover,
            http_client,
        })
    }

    fn parse_status(&mut self) -> Result<Status> {
        let json_str = self.prover.execute_json_status()?;
        let status: Status = serde_json::from_str(&json_str)?;
        Ok(status)
    }

    async fn handle_request(&self, req: &HttpRequest) -> Result<Vec<u8>> {
        let mut request_builder = match req.method.as_str() {
            "GET" => self.http_client.get(&req.url),
            "POST" => {
                let mut builder = self.http_client.post(&req.url);
                if let Some(body) = &req.body {
                    builder = builder.body(body.clone());
                }
                builder
            }
            method => {
                return Err(ColibriError::Ffi(format!("Unsupported HTTP method: {}", method)));
            }
        };

        // Add headers
        for (key, value) in &req.headers {
            request_builder = request_builder.header(key, value);
        }

        // Send request
        let response = request_builder.send().await?;
        let bytes = response.bytes().await?;

        Ok(bytes.to_vec())
    }

    pub async fn run_to_completion(&mut self) -> Result<Vec<u8>> {
        loop {
            match self.parse_status()? {
                Status::Pending { requests } => {
                    for request in requests {
                        match self.handle_request(&request).await {
                            Ok(data) => {
                                self.prover.set_response(
                                    request.request_id,
                                    &data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                self.prover.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Done => {
                    return self.prover.get_proof();
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }

    pub async fn run_to_completion_concurrent(&mut self) -> Result<Vec<u8>> {
        loop {
            match self.parse_status()? {
                Status::Pending { requests } => {
                    let futures: Vec<_> = requests
                        .iter()
                        .map(|req| self.handle_request(req))
                        .collect();

                    let results = futures::future::join_all(futures).await;

                    for (request, result) in requests.iter().zip(results.iter()) {
                        match result {
                            Ok(data) => {
                                self.prover.set_response(
                                    request.request_id,
                                    data,
                                    request.node_index,
                                );
                            }
                            Err(err) => {
                                self.prover.set_error(
                                    request.request_id,
                                    &err.to_string(),
                                    request.node_index,
                                )?;
                            }
                        }
                    }
                }
                Status::Done => {
                    return self.prover.get_proof();
                }
                Status::Error { message } => {
                    return Err(ColibriError::Ffi(message));
                }
            }
        }
    }
}