//! Chain identifiers and default endpoint URLs.
//!
//! Kept in sync with the Python bindings
//! (`bindings/python/src/colibri/client.py`).

/// Ethereum Mainnet.
pub const MAINNET: u64 = 1;
/// Ethereum Sepolia testnet.
pub const SEPOLIA: u64 = 11_155_111;
/// Gnosis Chain.
pub const GNOSIS: u64 = 100;
/// Chiado testnet (Gnosis).
pub const CHIADO: u64 = 10_200;

/// Default prover URLs per chain.
pub fn default_provers(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![
            "https://mainnet.colibri-proof.tech".into(),
            "https://mainnet-prover.incubed.net".into(),
            "https://mainnet.colimind.com".into(),
        ],
        SEPOLIA => vec![
            "https://sepolia.colibri-proof.tech".into(),
            "https://sepolia-prover.incubed.net".into(),
            "https://sepolia.colimind.com".into(),
        ],
        GNOSIS => vec![
            "https://gnosis.colibri-proof.tech".into(),
            "https://gnosis-prover.incubed.net".into(),
            "https://gnosis.colimind.com".into(),
        ],
        CHIADO => vec!["https://chiado.colibri-proof.tech".into()],
        _ => vec!["https://c4.incubed.net".into()],
    }
}

/// Default Ethereum RPC URLs per chain.
pub fn default_eth_rpcs(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![
            "https://mainnet.colibri-proof.tech/execution".into(),
            "https://eth.drpc.org".into(),
            "https://ethereum-rpc.publicnode.com".into(),
        ],
        SEPOLIA => vec![
            "https://sepolia.colibri-proof.tech/execution".into(),
            "https://sepolia.drpc.org".into(),
            "https://ethereum-sepolia-rpc.publicnode.com".into(),
        ],
        GNOSIS => vec![
            "https://gnosis.colibri-proof.tech/execution".into(),
            "https://rpc.gnosischain.com".into(),
            "https://gnosis-rpc.publicnode.com".into(),
        ],
        CHIADO => vec![
            "https://rpc.chiado.gnosis.gateway.fm".into(),
            "https://gnosis-chiado-rpc.publicnode.com".into(),
        ],
        _ => vec![],
    }
}

/// Default beacon API URLs per chain.
pub fn default_beacon_apis(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![
            "https://mainnet.colibri-proof.tech/consensus".into(),
            "https://ethereum-beacon-api.publicnode.com".into(),
        ],
        SEPOLIA => vec![
            "https://sepolia.colibri-proof.tech/consensus".into(),
            "https://ethereum-sepolia-beacon-api.publicnode.com".into(),
        ],
        GNOSIS => vec![
            "https://gnosis.colibri-proof.tech/consensus".into(),
            "https://gnosis-beacon-api.publicnode.com".into(),
        ],
        CHIADO => vec!["https://rpc-gbc.chiadochain.net".into()],
        _ => vec![],
    }
}

/// Default checkpointz URLs per chain. Only Mainnet, Sepolia, Gnosis,
/// and Chiado ship pre-configured lists; other chains return an empty
/// vector.
pub fn default_checkpointz(chain_id: u64) -> Vec<String> {
    match chain_id {
        MAINNET => vec![
            "https://sync-mainnet.beaconcha.in".into(),
            "https://mainnet.checkpoint.sigp.io".into(),
            "https://beaconstate-mainnet.chainsafe.io".into(),
            "https://beaconstate.ethstaker.cc".into(),
        ],
        SEPOLIA => vec![
            "https://checkpoint-sync.sepolia.ethpandaops.io".into(),
            "https://beaconstate-sepolia.chainsafe.io".into(),
        ],
        GNOSIS => vec!["https://checkpoint.gnosischain.com".into()],
        CHIADO => vec!["https://checkpoint.chiadochain.net".into()],
        _ => vec![],
    }
}
