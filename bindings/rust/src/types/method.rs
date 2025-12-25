use serde::{Deserialize, Serialize};

/// Represents the support level for an RPC method
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
pub enum MethodType {
    /// Method is proofable - can generate cryptographic proofs
    Proofable,
    /// Method is local - handled locally without proof generation
    Local,
    /// Method is not supported
    NotSupported,
}

impl MethodType {
    /// Convert from the C library's integer representation
    /// - 0: Not supported
    /// - 1+: Proofable (can generate proofs)
    /// - Negative: Reserved for future use
    pub fn from_support_code(code: i32) -> Self {
        match code {
            1.. => MethodType::Proofable,
            0 => MethodType::NotSupported,
            _ => MethodType::NotSupported,  // Negative values also mean not supported
        }
    }

    /// Check if the method is supported (either proofable or local)
    pub fn is_supported(&self) -> bool {
        matches!(self, MethodType::Proofable | MethodType::Local)
    }

    /// Check if the method requires proof generation
    pub fn requires_proof(&self) -> bool {
        matches!(self, MethodType::Proofable)
    }
}