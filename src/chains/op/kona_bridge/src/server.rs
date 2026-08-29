// server.rs - Axum HTTP-Server mit SSE-Notifications für den Standalone-Service

use crate::types::KonaBridgeStats;
use axum::{
    body::Body,
    extract::{Path, State},
    http::{header, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Router,
};
use axum::response::sse::{Event, KeepAlive, Sse};
use futures_util::stream::Stream;
use std::{convert::Infallible, path::PathBuf, sync::{Arc, Mutex}};
use tokio::sync::broadcast;
use tokio_stream::wrappers::BroadcastStream;
use tokio_stream::StreamExt;
use tracing::error;

struct AppState {
    output_dir: PathBuf,
    chain_id:   u64,
    stats:      Arc<Mutex<KonaBridgeStats>>,
    sse_tx:     broadcast::Sender<u64>,
}

pub async fn run_http_server(
    output_dir: PathBuf,
    chain_id: u64,
    port: u16,
    stats: Arc<Mutex<KonaBridgeStats>>,
    sse_tx: broadcast::Sender<u64>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let state = Arc::new(AppState { output_dir, chain_id, stats, sse_tx });

    let app = Router::new()
        .route("/preconf/latest",     get(serve_latest))
        .route("/preconf/pre_latest", get(serve_pre_latest))
        .route("/preconf/{block_hex}", get(serve_block))
        .route("/stats",              get(serve_stats))
        .route("/metrics",            get(serve_metrics))
        .route("/sse",                get(sse_handler))
        .with_state(state);

    let listener = tokio::net::TcpListener::bind(format!("0.0.0.0:{}", port)).await?;
    tracing::info!("🌐 HTTP server listening on 0.0.0.0:{}", port);
    axum::serve(listener, app).await?;
    Ok(())
}

async fn serve_latest(State(state): State<Arc<AppState>>) -> impl IntoResponse {
    serve_raw_file(state.output_dir.join("latest.raw")).await
}

async fn serve_pre_latest(State(state): State<Arc<AppState>>) -> impl IntoResponse {
    serve_raw_file(state.output_dir.join("pre_latest.raw")).await
}

async fn serve_block(
    State(state): State<Arc<AppState>>,
    Path(block_hex): Path<String>,
) -> impl IntoResponse {
    let hex_str = block_hex.trim_start_matches("0x").trim_start_matches("0X");
    let block_number = match u64::from_str_radix(hex_str, 16) {
        Ok(n) => n,
        Err(_) => return (StatusCode::BAD_REQUEST, "Invalid block number").into_response(),
    };
    let filename = format!("block_{}_{}.raw", state.chain_id, block_number);
    serve_raw_file(state.output_dir.join(filename)).await
}

async fn serve_raw_file(path: PathBuf) -> Response<Body> {
    match tokio::fs::read(&path).await {
        Ok(data) => (
            [(header::CONTENT_TYPE, "application/octet-stream")],
            data,
        ).into_response(),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            StatusCode::NOT_FOUND.into_response()
        }
        Err(e) => {
            error!("Failed to read file {:?}: {}", path, e);
            StatusCode::INTERNAL_SERVER_ERROR.into_response()
        }
    }
}

async fn serve_stats(State(state): State<Arc<AppState>>) -> impl IntoResponse {
    let s = state.stats.lock().unwrap();
    axum::Json(serde_json::json!({
        "connected_peers":    s.connected_peers,
        "received_preconfs":  s.received_preconfs,
        "processed_preconfs": s.processed_preconfs,
        "failed_preconfs":    s.failed_preconfs,
        "http_received":      s.http_received,
        "http_processed":     s.http_processed,
        "gossip_received":    s.gossip_received,
        "gossip_processed":   s.gossip_processed,
        "mode_switches":      s.mode_switches,
        "current_mode":       s.current_mode,
        "total_gaps":         s.total_gaps,
        "http_gaps":          s.http_gaps,
        "gossip_gaps":        s.gossip_gaps,
        "bitmask_gaps":       s.bitmask_gaps,
        "last_block_number":  s.last_block_number,
        "last_preconf_unix":  s.last_preconf_unix,
    }))
}

/// Prometheus text exposition (0.0.4) for the standalone preconf service.
fn prometheus_metrics(chain_id: u64, s: &KonaBridgeStats) -> String {
    format!(
        "\
# HELP colibri_kona_last_preconf_timestamp_seconds Unix time of the last processed preconf.\n\
# TYPE colibri_kona_last_preconf_timestamp_seconds gauge\n\
colibri_kona_last_preconf_timestamp_seconds{{chain_id=\"{chain_id}\"}} {ts}\n\
# HELP colibri_kona_preconfs_total Total processed preconfs.\n\
# TYPE colibri_kona_preconfs_total counter\n\
colibri_kona_preconfs_total{{chain_id=\"{chain_id}\"}} {total}\n\
# HELP colibri_kona_peers Currently connected gossip peers.\n\
# TYPE colibri_kona_peers gauge\n\
colibri_kona_peers{{chain_id=\"{chain_id}\"}} {peers}\n\
# HELP colibri_kona_last_block_number Last processed L2 block number.\n\
# TYPE colibri_kona_last_block_number gauge\n\
colibri_kona_last_block_number{{chain_id=\"{chain_id}\"}} {block}\n",
        ts = s.last_preconf_unix,
        total = s.processed_preconfs,
        peers = s.connected_peers,
        block = s.last_block_number,
    )
}

async fn serve_metrics(State(state): State<Arc<AppState>>) -> impl IntoResponse {
    let body = match state.stats.lock() {
        Ok(s) => prometheus_metrics(state.chain_id, &s),
        Err(poisoned) => prometheus_metrics(state.chain_id, &poisoned.into_inner()),
    };
    (
        [(header::CONTENT_TYPE, "text/plain; version=0.0.4; charset=utf-8")],
        body,
    )
}

async fn sse_handler(
    State(state): State<Arc<AppState>>,
) -> Sse<impl Stream<Item = Result<Event, Infallible>>> {
    let rx = state.sse_tx.subscribe();
    let stream = BroadcastStream::new(rx).filter_map(|res| {
        res.ok().map(|block_number| {
            Ok(Event::default()
                .event("preconf")
                .data(block_number.to_string()))
        })
    });
    Sse::new(stream).keep_alive(KeepAlive::default())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::KonaBridgeStats;

    #[test]
    fn prometheus_exposes_requested_series() {
        let s = KonaBridgeStats {
            connected_peers: 2,
            processed_preconfs: 7,
            last_block_number: 50_574_674,
            last_preconf_unix: 1_756_400_000,
            ..KonaBridgeStats::default()
        };
        let body = prometheus_metrics(8453, &s);
        assert!(body.contains(
            "colibri_kona_last_preconf_timestamp_seconds{chain_id=\"8453\"} 1756400000"
        ));
        assert!(body.contains("colibri_kona_preconfs_total{chain_id=\"8453\"} 7"));
        assert!(body.contains("colibri_kona_peers{chain_id=\"8453\"} 2"));
        assert!(body.contains(
            "colibri_kona_last_block_number{chain_id=\"8453\"} 50574674"
        ));
    }
}
