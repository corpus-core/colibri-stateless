// config.rs - Chain-Konfigurationen für OP-Stack-Chains

use alloy_primitives::{address, Address};
use discv5::Enr;

/// Chain-specific configuration used by the bridge.
///
/// Gossip bootstraps from `kona_peers::BootNodes::from_chain_id`. `bootnodes`
/// is an optional extra set (empty by default) that is merged on top.
#[derive(Clone)]
pub struct ChainConfig {
    pub unsafe_signer: Address,
    pub chain_id: u64,
    pub bootnodes: Vec<Enr>,
}

impl ChainConfig {
    /// HTTP-Endpoint für die Chain abrufen
    pub fn get_http_endpoint(&self) -> Option<String> {
        // Get HTTP endpoint from centralized chain configuration
        match self.chain_id {
            10 => Some("https://op-mainnet.operationsolarstorm.org/latest".to_string()),
            8453 => Some("https://base.operationsolarstorm.org/latest".to_string()),
            130 => Some("https://unichain.operationsolarstorm.org/latest".to_string()),
            480 => Some("https://worldchain.operationsolarstorm.org/latest".to_string()),
            7777777 => Some("https://zora.operationsolarstorm.org/latest".to_string()),
            _ => None,
        }
    }

    /// Fallback HTTP-Endpoints für bessere Abdeckung
    #[allow(dead_code)]
    pub fn get_fallback_endpoints(&self) -> Vec<String> {
        match self.chain_id {
            8453 => vec![
                "https://base.operationsolarstorm.org/latest".to_string(),
                // Könnten hier weitere Base-Endpoints hinzufügen wenn verfügbar
            ],
            10 => vec!["https://op-mainnet.operationsolarstorm.org/latest".to_string()],
            _ => vec![],
        }
    }

    /// Preset name for a chain id this bridge knows, or `None` when the caller
    /// must supply the sequencer and a kona rollup config itself.
    pub fn preset_name(chain_id: u64) -> Option<&'static str> {
        match chain_id {
            10 => Some("op-mainnet"),
            8453 => Some("base"),
            130 => Some("unichain"),
            480 => Some("worldchain"),
            7777777 => Some("zora"),
            _ => None,
        }
    }

    /// Chain-Konfiguration aus Netzwerk-Name erstellen
    pub fn from(network: &str, chain_id: u64) -> Self {
        match network {
            "op-mainnet" => ChainConfig {
                unsafe_signer: address!("AAAA45d9549EDA09E70937013520214382Ffc4A2"),
                chain_id: 10,
                bootnodes: Vec::new(),
            },
            "base" => ChainConfig {
                unsafe_signer: address!("Af6E19BE0F9cE7f8afd49a1824851023A8249e8a"),
                chain_id: 8453,
                bootnodes: Vec::new(),
            },
            "unichain" => ChainConfig {
                unsafe_signer: address!("833C6f278474A78658af91aE8edC926FE33a230e"),
                chain_id: 130,
                bootnodes: Vec::new(),
            },
            "worldchain" => ChainConfig {
                unsafe_signer: address!("2270d6eC8E760daA317DD978cFB98C8f144B1f3A"),
                chain_id: 480,
                bootnodes: Vec::new(),
            },
            "zora" => ChainConfig {
                // L1 SystemConfig.unsafeBlockSigner() at 0xA3cAB0126d5F504B071b81a3e8A2BBBF17930d86.
                unsafe_signer: address!("3Dc8Dfd0709C835cAd15a6A27e089FF4cF4C9228"),
                chain_id: 7777777,
                bootnodes: Vec::new(),
            },
            _ => {
                // Use provided chain_id for custom networks
                ChainConfig {
                    unsafe_signer: Address::ZERO, // Will be configured from C config
                    chain_id,
                    bootnodes: Vec::new(),
                }
            }
        }
    }
}
