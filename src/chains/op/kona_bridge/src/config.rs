// config.rs - Chain-Konfigurationen für OP-Stack-Chains

use alloy::primitives::{address, Address};
use discv5::Enr;

/// Chain-spezifische Konfiguration
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

    /// Chain-Konfiguration aus Netzwerk-Name erstellen
    pub fn from(network: &str, chain_id: u64) -> Self {
        match network {
            "op-mainnet" => ChainConfig {
                unsafe_signer: address!("AAAA45d9549EDA09E70937013520214382Ffc4A2"),
                chain_id: 10,
                // Use the same working bootnodes as Unichain for now
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "base" => ChainConfig {
                unsafe_signer: address!("Af6E19BE0F9cE7f8afd49a1824851023A8249e8a"),
                chain_id: 8453,
                // Use the same working bootnodes as Unichain for now
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "unichain" => ChainConfig {
                unsafe_signer: address!("833C6f278474A78658af91aE8edC926FE33a230e"),
                chain_id: 130,
                bootnodes: vec![
                    "enr:-Iq4QNqqxkwND5YdrKxSVR8RoZHwU6Qa42ff_0XNjD428_n9OTEy3N9iR4uZTfQxACB00fT7Y8__q238kpb6TcsRvw-GAZZoqRJLgmlkgnY0gmlwhDQOHieJc2VjcDI1NmsxoQLqnqr2lfrL5TCQvrelsEEagUWbv25sqsFR5YfudxIKG4N1ZHCCdl8",
                    "enr:-Iq4QBtf4EkiX7NfYxCn6CKIh3ZJqjk70NWS9hajT1k3W7-3ePWBc5-g19tBqYAMWlfSSz3sir024EQc5YH3TAxVY76GAZZopWrWgmlkgnY0gmlwhAOUZK2Jc2VjcDI1NmsxoQN3trHnKYTV1Q4ArpNP_qmCkCIm_pL6UNpCM0wnUNjkBYN1ZHCCdl8",
                ]
                .iter()
                .map(|v| v.parse().unwrap())
                .collect(),
            },
            "worldchain" => ChainConfig {
                unsafe_signer: address!("2270d6eC8E760daA317DD978cFB98C8f144B1f3A"),
                chain_id: 480,
                bootnodes: Vec::new(),
            },
            "zora" => ChainConfig {
                unsafe_signer: address!("3Dc8Dfd070C835cAd15a6A27e089FF4cF4C92280"),
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
