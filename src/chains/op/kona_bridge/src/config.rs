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
                unsafe_signer: address!("FaACA0A10e4a23C5E01485DE07767547d4D8056D"),
                chain_id: 130,
                bootnodes: Vec::new(),
            },
            "worldchain" => ChainConfig {
                unsafe_signer: address!("2270d6eC8E760daA317DD978cFB98C8f144B1f3A"),
                chain_id: 480,
                bootnodes: Vec::new(),
            },
            "zora" => ChainConfig {
                unsafe_signer: address!("3Dc8Dfd0709C835cAd15a6A27e089FF4cF4C9228"),
                chain_id: 7777777,
                bootnodes: Vec::new(),
            },
            // No HTTP rollup endpoint. Gossip uses kona_peers bootnodes only.
            "ink" => ChainConfig {
                unsafe_signer: address!("7b322282DF45E537E5de76D60E1432Db3cF3F8E1"),
                chain_id: 57073,
                bootnodes: Vec::new(),
            },
            _ => {
                // Not a Base fallback. Callers that join a network use `from_chain_id`.
                ChainConfig {
                    unsafe_signer: Address::ZERO,
                    chain_id,
                    bootnodes: Vec::new(),
                }
            }
        }
    }

    /// Build the preset for `chain_id`.
    ///
    /// Unknown ids are rejected. Mapping them onto Base would make the process
    /// gossip on Base while the operator believes it joined another chain.
    /// The chain id inside a named preset is fixed; the argument is not copied
    /// into the Base preset.
    pub fn from_chain_id(chain_id: u64) -> Result<Self, String> {
        let network = match chain_id {
            10 => "op-mainnet",
            8453 => "base",
            130 => "unichain",
            480 => "worldchain",
            7777777 => "zora",
            57073 => "ink",
            _ => {
                return Err(format!(
                    "unsupported OP-Stack chain id {chain_id}: refusing to fall back to Base"
                ));
            }
        };
        let config = Self::from(network, chain_id);
        if config.chain_id != chain_id {
            return Err(format!(
                "preset {network} has chain id {}, not {chain_id}",
                config.chain_id
            ));
        }
        Ok(config)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn presets_keep_onchain_signers_and_chain_ids() {
        let unichain = ChainConfig::from_chain_id(130).unwrap();
        assert_eq!(unichain.chain_id, 130);
        assert_eq!(
            unichain.unsafe_signer,
            address!("FaACA0A10e4a23C5E01485DE07767547d4D8056D")
        );
        assert_ne!(
            unichain.unsafe_signer,
            address!("833C6f278474A78658af91aE8edC926FE33a230e")
        );

        let zora = ChainConfig::from_chain_id(7777777).unwrap();
        assert_eq!(
            zora.unsafe_signer,
            address!("3Dc8Dfd0709C835cAd15a6A27e089FF4cF4C9228")
        );

        let ink = ChainConfig::from_chain_id(57073).unwrap();
        assert_eq!(ink.chain_id, 57073);
        assert_eq!(
            ink.unsafe_signer,
            address!("7b322282DF45E537E5de76D60E1432Db3cF3F8E1")
        );
        assert!(ink.get_http_endpoint().is_none());
    }

    #[test]
    fn unknown_chain_does_not_join_base() {
        match ChainConfig::from_chain_id(57074) {
            Ok(_) => panic!("unknown chain id joined a preset"),
            Err(err) => assert!(err.contains("refusing to fall back to Base")),
        }
        assert_eq!(ChainConfig::from_chain_id(8453).unwrap().chain_id, 8453);
    }
}
