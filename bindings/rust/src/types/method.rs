use serde::{Deserialize, Serialize};

/// Support level returned by [`c4_get_method_support`] for an RPC method.
///
/// The values match the underlying `c4_method_type_t` enum in the C core
/// (see `bindings/colibri.h`).
///
/// [`c4_get_method_support`]: crate::ffi::c4_get_method_support
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
#[repr(i32)]
pub enum MethodType {
    /// Method is not defined / not recognised.
    Undefined = 0,
    /// Method can be cryptographically proven -- run a proof + verify.
    Proofable = 1,
    /// Method exists but cannot be proven -- call the RPC directly.
    Unproofable = 2,
    /// Method is not supported by Colibri at all.
    NotSupported = 3,
    /// Method can be computed locally (e.g. `eth_chainId`).
    Local = 4,
}

impl MethodType {
    /// Map the raw integer returned by `c4_get_method_support` to
    /// [`MethodType`]. Unknown values fall back to [`MethodType::Undefined`].
    pub fn from_support_code(code: i32) -> Self {
        match code {
            1 => MethodType::Proofable,
            2 => MethodType::Unproofable,
            3 => MethodType::NotSupported,
            4 => MethodType::Local,
            _ => MethodType::Undefined,
        }
    }

    /// `true` for methods that produce a meaningful result via Colibri --
    /// either through a proof, a direct RPC pass-through or a local
    /// computation.
    pub fn is_supported(&self) -> bool {
        !matches!(self, MethodType::NotSupported | MethodType::Undefined)
    }

    /// `true` when the method is expected to go through the full proof
    /// generation + verification pipeline.
    pub fn requires_proof(&self) -> bool {
        matches!(self, MethodType::Proofable)
    }
}

/// Privacy mode -- Pragmatic Adaptive Privacy (see `VERIFY_FLAG_PAP`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PrivacyMode {
    /// No privacy features. Default.
    #[default]
    None,
    /// PAP basic mode (verify flag `1 << 1`).
    Basic,
}

/// Proof generation mode -- matches `c4_prover_mode_t` in the C core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ProverMode {
    /// Generate proofs locally.
    Local = 0,
    /// Ask a remote prover for the proof.
    Remote = 1,
    /// Try local first, fall back to remote.
    Hybrid = 2,
    /// Proxy raw RPC through the configured endpoints without proving.
    Proxy = 3,
    /// Like [`ProverMode::Hybrid`] but with a background poller
    /// warming the block-header cache.
    LightClient = 4,
}

impl ProverMode {
    /// Native representation understood by `c4_create_rpc_ctx`. The
    /// `LightClient` mode maps to `Hybrid` on the C side; the
    /// distinction only matters for the Rust host (background poller).
    pub(crate) fn as_native(self) -> i32 {
        match self {
            ProverMode::LightClient => ProverMode::Hybrid as i32,
            other => other as i32,
        }
    }
}
