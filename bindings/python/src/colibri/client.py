"""
Main Colibri client implementation
"""

import asyncio
import json
from typing import Any, Dict, List, Optional, Union

import aiohttp

from .storage import ColibriStorage, DefaultStorage
from .types import (
    ColibriError,
    DataRequest,
    HTTPError,
    MethodType,
    PrivacyMode,
    ProofError,
    ProverMode,
    RevertError,
    RPCError,
    VerificationError,
)

# Import the native module (will be built with pybind11)
# Use lazy import to avoid circular import issues
_native = None

def _get_native():
    """Lazy import of native module to avoid circular imports"""
    global _native
    if _native is None:
        try:
            from . import _native as native_module
            _native = native_module
        except ImportError:
            # Fallback for development/testing without compiled module
            _native = False  # Mark as attempted but failed
    return _native if _native is not False else None


class Colibri:
    """
    Main Colibri client for stateless Ethereum proof generation and verification
    """

    def __init__(
        self,
        chain_id: int = 1,
        provers: List[str] = None,
        eth_rpcs: List[str] = None,
        beacon_apis: List[str] = None,
        checkpointz: List[str] = None,
        oblivious_nodes: Optional[List[str]] = None,
        trusted_checkpoint: Optional[str] = None,
        include_code: bool = False,
        use_accesslist: bool = False,
        zk_proof: bool = False,
        privacy_mode: Optional[PrivacyMode] = None,
        prover_mode: Optional['ProverMode'] = None,
        checkpoint_witness_keys: Optional[str] = None,
        skip_wsp_check: bool = False,
        storage: Optional[ColibriStorage] = None,
        request_handler: Optional[Any] = None,  # For testing
    ):
        """
        Initialize Colibri client
        
        Args:
            chain_id: Blockchain chain ID (default: 1 for Ethereum Mainnet)
            provers: List of prover server URLs
            eth_rpcs: List of Ethereum RPC URLs
            beacon_apis: List of beacon chain API URLs
            checkpointz: List of checkpointz server URLs
            oblivious_nodes: TEE RPC endpoints for eth_getProof (privacy-preserving storage reads)
            trusted_checkpoint: Optional trusted checkpoint as hex string (0x-prefixed, 66 chars)
            include_code: Whether to include code in proofs
            use_accesslist: Whether to use eth_createAccessList instead of debug_traceCall
            zk_proof: Whether to request ZK sync proofs from remote provers
            privacy_mode: PAP mode (PrivacyMode.NONE or PrivacyMode.BASIC). Default NONE.
            checkpoint_witness_keys: Optional hex-encoded witness signer keys (0x-prefixed)
            skip_wsp_check: If True, set VERIFY_FLAG_SKIP_WSP_CHECK (bit 1<<7) and skip the
                Weak Subjectivity Period check. SECURITY: only safe with an alternative trust
                anchor (witness signatures, hard-coded checkpoint, signed package); disabling
                raises the risk of long-range attacks across periods older than the WSP.
                Default: False.
            storage: Storage implementation (defaults to DefaultStorage)
            request_handler: Optional request handler for testing
        """
        self.chain_id = chain_id
        # Fix Python falsy-array bug: [] or default returns default!
        self.provers = provers if provers is not None else self._get_default_provers(chain_id)
        self.eth_rpcs = eth_rpcs if eth_rpcs is not None else self._get_default_eth_rpcs(chain_id)
        self.beacon_apis = beacon_apis if beacon_apis is not None else self._get_default_beacon_apis(chain_id)
        self.checkpointz = checkpointz if checkpointz is not None else self._get_default_checkpointz(chain_id)
        self.oblivious_nodes = oblivious_nodes if oblivious_nodes is not None else []
        self.trusted_checkpoint = trusted_checkpoint
        self.include_code = include_code
        self.use_accesslist = use_accesslist
        self.zk_proof = zk_proof
        self.privacy_mode = privacy_mode if privacy_mode is not None else PrivacyMode.NONE
        self.prover_mode = prover_mode
        self.checkpoint_witness_keys = checkpoint_witness_keys
        self.skip_wsp_check = skip_wsp_check
        self.request_handler = request_handler
        self._light_client_task: Optional[asyncio.Task] = None

        # Initialize storage - registration is global in C
        # The first instance determines the global storage type for C operations
        from . import _register_global_storage
        
        if storage is None:
            storage = DefaultStorage()
            
        # Register storage globally (first call sets it, subsequent calls return the global one)
        global_storage = _register_global_storage(storage)
        
        # For local operations, we use the requested storage
        # For C operations, the global storage is used automatically
        self.storage = storage
        
        # Store reference to global storage for clarity
        self._global_storage = global_storage

    @staticmethod
    def _get_default_provers(chain_id: int) -> List[str]:
        """Get default prover URLs for chain"""
        defaults = {
            1: ["https://mainnet1.colibri-proof.tech"],
            11155111: ["https://sepolia.colibri-proof.tech"],
            100: ["https://gnosis.colibri-proof.tech"],
            10200: ["https://chiado.colibri-proof.tech"],
        }
        return defaults.get(chain_id, ["https://c4.incubed.net"])

    @staticmethod
    def _get_default_eth_rpcs(chain_id: int) -> List[str]:
        """Get default Ethereum RPC URLs for chain"""
        defaults = {
            1: ["https://rpc.ankr.com/eth"],
            11155111: ["https://ethereum-sepolia-rpc.publicnode.com"],
            100: ["https://rpc.ankr.com/gnosis"],
            10200: ["https://gnosis-chiado-rpc.publicnode.com"],
        }
        return defaults.get(chain_id, ["https://rpc.ankr.com/eth"])

    @staticmethod
    def _get_default_beacon_apis(chain_id: int) -> List[str]:
        """Get default beacon API URLs for chain"""
        defaults = {
            1: ["https://lodestar-mainnet.chainsafe.io"],
            11155111: ["https://ethereum-sepolia-beacon-api.publicnode.com"],
            100: ["https://gnosis.colibri-proof.tech"],
            10200: ["https://gnosis-chiado-beacon-api.publicnode.com"],
        }
        return defaults.get(chain_id, ["https://lodestar-mainnet.chainsafe.io"])

    @staticmethod
    def _get_default_checkpointz(chain_id: int) -> List[str]:
        """Get default checkpointz URLs for chain"""
        defaults = {
            1: [
                "https://sync-mainnet.beaconcha.in",
                "https://beaconstate.info",
                "https://sync.invis.tools",
                "https://beaconstate.ethstaker.cc",
            ],
            11155111: [
                "https://sepolia.beaconstate.info",
                "https://checkpoint-sync.sepolia.ethpandaops.io",
            ],
            100: ["https://checkpoint.gnosischain.com"],
            10200: ["https://checkpoint.chiadochain.net"],
        }
        return defaults.get(chain_id, [])

    def _get_verify_flags(self) -> int:
        """Return verify flags for C API (e.g. VERIFY_FLAG_PAP = 2, VERIFY_FLAG_OBLIVIOUS = 64, VERIFY_FLAG_SKIP_WSP_CHECK = 128)."""
        pap = self.privacy_mode == PrivacyMode.BASIC or bool(self.oblivious_nodes)
        flags = 2 if pap else 0
        if self.oblivious_nodes:
            flags |= 1 << 6
        if self.skip_wsp_check:
            flags |= 1 << 7
        return flags

    def get_method_support(self, method: str, params: Optional[List[Any]] = None) -> MethodType:
        """
        Check what type of support a method has.

        In PAP mode the result may depend on cached data for the given params
        (e.g. `eth_call` can become LOCAL when storage values are cached).

        @param method RPC method name
        @param params Optional method parameters (used in PAP mode for cache lookup)
        @return MethodType indicating the support level
        """
        native = _get_native()
        if native and hasattr(native, 'get_method_support'):
            try:
                import json
                params_str = json.dumps(params) if params else ""
                type_int = native.get_method_support(self.chain_id, method, params_str, self._get_verify_flags())
                return MethodType(type_int)
            except (ValueError, TypeError):
                return MethodType.UNDEFINED
        
        # Fallback implementation for testing
        proofable_methods = {
            "eth_getBalance", "eth_getCode", "eth_getStorageAt",
            "eth_getTransactionByHash", "eth_getTransactionReceipt",
            "eth_getBlockByHash", "eth_getBlockByNumber", "eth_getLogs",
            "eth_call", "eth_getProof", "eth_getTransactionCount"
        }
        
        local_methods = {"eth_chainId", "net_version"}
        
        if method in proofable_methods:
            return MethodType.PROOFABLE
        elif method in local_methods:
            return MethodType.LOCAL
        elif method.startswith("eth_"):
            return MethodType.UNPROOFABLE
        else:
            return MethodType.UNDEFINED

    async def create_proof(self, method: str, params: List[Any]) -> bytes:
        """
        Create a proof for the given method and parameters
        
        Args:
            method: RPC method name
            params: Method parameters
            
        Returns:
            Proof data as bytes
            
        Raises:
            ProofError: If proof creation fails
        """
        native = _get_native()
        if not native:
            raise ProofError("Native module not available")

        try:
            # Create prover context
            params_json = json.dumps(params)
            prover_flags = (1 if self.include_code else 0) | ((1 << 6) if self.use_accesslist else 0)
            ctx = native.create_prover_ctx(
                method, 
                params_json, 
                self.chain_id, 
                prover_flags
            )
            
            if not ctx:
                raise ProofError(f"Failed to create prover context for {method}")

            try:
                # Execute proof generation with request handling
                while True:
                    status_json = native.prover_execute_json_status(ctx)
                    if not status_json:
                        raise ProofError("Prover execution returned null")
                    
                    status = json.loads(status_json)
                    
                    if status["status"] == "success":
                        return native.prover_get_proof(ctx)
                    elif status["status"] == "error":
                        raise ProofError(status.get("error", "Unknown proof error"))
                    elif status["status"] == "pending":
                        await self._handle_requests(status.get("requests", []))
                    else:
                        raise ProofError(f"Unknown status: {status['status']}")
            
            finally:
                native.free_prover_ctx(ctx)
                
        except json.JSONDecodeError as e:
            raise ProofError(f"Invalid JSON in proof response: {e}") from e
        except Exception as e:
            if isinstance(e, ProofError):
                raise
            raise ProofError(f"Proof creation failed: {e}") from e

    async def verify_proof(
        self, 
        proof: bytes, 
        method: str, 
        params: List[Any]
    ) -> Any:
        """
        Verify a proof and return the result
        
        Args:
            proof: Proof data as bytes
            method: RPC method name
            params: Method parameters
            
        Returns:
            Verification result
            
        Raises:
            VerificationError: If verification fails
        """
        native = _get_native()
        if not native:
            raise VerificationError("Native module not available")

        try:
            # Create verification context
            params_json = json.dumps(params)
            trusted_checkpoint_str = self.trusted_checkpoint if self.trusted_checkpoint else ""
            
            ctx = native.create_verify_ctx(
                proof,
                method,
                params_json,
                self.chain_id,
                trusted_checkpoint_str,
                self._get_verify_flags()
            )
            
            if not ctx:
                raise VerificationError(f"Failed to create verification context for {method}")

            try:
                # Execute verification with request handling
                while True:
                    status_json = native.verify_execute_json_status(ctx)
                    if not status_json:
                        raise VerificationError("Verification execution returned null")
                    
                    # Parse JSON response from C library
                    try:
                        status = json.loads(status_json)
                    except json.JSONDecodeError as e:
                        # JSON parsing failed - this indicates a bug in the C library
                        raise VerificationError(f"Invalid JSON from C library: {e}") from e
                    
                    if status["status"] == "success":
                        return status.get("result")
                    elif status["status"] == "revert":
                        raise RevertError(status.get("data", "0x"))
                    elif status["status"] == "error":
                        raise VerificationError(status.get("error", "Unknown verification error"))
                    elif status["status"] == "pending":
                        await self._handle_requests(status.get("requests", []), use_prover_fallback=True)
                    else:
                        raise VerificationError(f"Unknown status: {status['status']}")
            
            finally:
                native.verify_free_ctx(ctx)
                
        # Propagate VerificationError and RevertError as-is. RevertError is a
        # fully verified outcome (the EVM ran to completion but reverted) --
        # callers need it intact to decode the data for EIP-3668 / CCIP-Read or
        # custom Solidity errors.
        except (VerificationError, RevertError):
            raise
        except json.JSONDecodeError as e:
            raise VerificationError(f"Invalid JSON in verification response: {e}") from e
        except Exception as e:
            raise VerificationError(f"Proof verification failed: {e}") from e

    async def rpc(self, method: str, params: List[Any]) -> Any:
        """
        Execute an RPC call via the unified C core state machine.
        
        Args:
            method: RPC method name
            params: Method parameters
            
        Returns:
            Verified RPC result
            
        Raises:
            ColibriError: If the RPC call fails
        """
        native = _get_native()
        if not native:
            raise ColibriError("Native module not available")

        params_json = json.dumps(params)
        prover_flags = (1 if self.include_code else 0) | ((1 << 6) if self.use_accesslist else 0) | ((1 << 7) if self.zk_proof else 0)
        resolved_mode = self.prover_mode if self.prover_mode is not None else (ProverMode.REMOTE if self.provers else ProverMode.LOCAL)
        native_mode = int(ProverMode.HYBRID) if resolved_mode == ProverMode.LIGHT_CLIENT else int(resolved_mode)

        ctx = native.create_rpc_ctx(method, params_json, self.chain_id,
                                    prover_flags, self._get_verify_flags(), native_mode)
        if not ctx:
            raise ColibriError(f"Failed to create RPC context for {method}")

        if resolved_mode == ProverMode.PROXY:
            native.rpc_set_proxy_urls(ctx, ",".join(self.eth_rpcs), ",".join(self.beacon_apis))

        if self.trusted_checkpoint:
            native.set_checkpoint(self.chain_id, self.trusted_checkpoint)
        if self.checkpoint_witness_keys:
            native.rpc_set_witness_keys(ctx, self.checkpoint_witness_keys)

        try:
            while True:
                status_json = native.rpc_execute_json_status(ctx)
                if not status_json:
                    raise ColibriError("RPC execution returned null")

                status = json.loads(status_json)

                if status["status"] == "success":
                    return status.get("result")
                elif status["status"] == "revert":
                    raise RevertError(status.get("data", "0x"))
                elif status["status"] == "error":
                    raise ColibriError(status.get("error", "Unknown RPC error"))
                elif status["status"] == "pending":
                    await self._handle_requests(status.get("requests", []), use_prover_fallback=True)
                else:
                    raise ColibriError(f"Unknown status: {status['status']}")
        finally:
            native.free_rpc_ctx(ctx)

    async def start_light_client(self, interval: float = 12.0, full_block: bool = False) -> None:
        """
        Start background polling to keep the block header cache warm.
        Useful for ``ProverMode.LIGHT_CLIENT``.

        By default polls ``eth_getBlockHeader("latest")`` which fetches only the
        compact header proof. Set *full_block* to ``True`` to poll
        ``eth_getBlockByNumber("latest")`` instead -- useful when many
        ``eth_getTransactionByHash`` / ``eth_getTransactionReceipt`` calls follow,
        since those need the full block data.

        Args:
            interval: polling interval in seconds (default: 12 = one Ethereum slot)
            full_block: if True, fetch the full block instead of just the header
        """
        self.stop_light_client()
        method = "eth_getBlockByNumber" if full_block else "eth_getBlockHeader"
        params = ["latest", False] if full_block else ["latest"]

        async def _poll():
            while True:
                try:
                    await self.rpc(method, params)
                except Exception:
                    pass
                await asyncio.sleep(interval)

        self._light_client_task = asyncio.create_task(_poll())

    def stop_light_client(self) -> None:
        """Stop background light client polling."""
        if self._light_client_task is not None:
            self._light_client_task.cancel()
            self._light_client_task = None

    async def _handle_requests(
        self, 
        requests: List[Dict[str, Any]], 
        use_prover_fallback: bool = False
    ) -> None:
        """
        Handle pending data requests from the C library
        
        Args:
            requests: List of request dictionaries
            use_prover_fallback: Whether to use prover URLs for beacon API requests
        """
        async def handle_single_request(request_dict: Dict[str, Any]) -> None:
            try:
                request = DataRequest.from_dict(request_dict)
                
                # Mock request handling for testing
                if self.request_handler:
                    try:
                        response_data = await self.request_handler.handle_request(request)
                        native = _get_native()
                        if native:
                            native.req_set_response(request.req_ptr, response_data, 0)
                        return
                    except Exception as e:
                        native = _get_native()
                        if native:
                            native.req_set_error(request.req_ptr, str(e), 0)
                        return

                # Determine server list
                if request.request_type == "checkpointz":
                    servers = list(self.checkpointz) + list(self.beacon_apis)
                elif request.request_type == "prover":
                    servers = self.provers
                elif request.request_type == "beacon_api":
                    if use_prover_fallback and self.provers:
                        servers = self.provers
                    else:
                        servers = self.beacon_apis
                elif (
                    request.request_type == "eth_rpc"
                    and request.payload
                    and request.payload.get("method") == "eth_getProof"
                    and self.oblivious_nodes
                ):
                    servers = self.oblivious_nodes
                else:
                    servers = self.eth_rpcs

                # Execute HTTP request
                try:
                    response_data = await self._execute_http_request(request, servers)
                    native = _get_native()
                    if native:
                        native.req_set_response(request.req_ptr, response_data, 0)
                except Exception as e:
                    native = _get_native()
                    if native:
                        native.req_set_error(request.req_ptr, str(e), 0)

            except Exception as e:
                # Handle any unexpected errors in request processing
                print(f"Error handling request: {e}")
                native = _get_native()
                if native and "req_ptr" in request_dict:
                    try:
                        native.req_set_error(request_dict["req_ptr"], str(e), 0)
                    except Exception:
                        pass  # Ignore errors in error reporting

        # Execute all requests concurrently
        await asyncio.gather(
            *[handle_single_request(req) for req in requests],
            return_exceptions=True
        )

    async def _execute_http_request(
        self, 
        request: DataRequest, 
        servers: List[str]
    ) -> bytes:
        """
        Execute a single HTTP request against multiple servers
        
        Args:
            request: The data request to execute
            servers: List of server URLs to try
            
        Returns:
            Response data as bytes
            
        Raises:
            HTTPError: If all servers fail
        """
        async with aiohttp.ClientSession() as session:
            for i, server in enumerate(servers):
                # Skip excluded servers
                if request.exclude_mask & (1 << i):
                    continue
                
                # Build URL
                if request.url:
                    url = f"{server.rstrip('/')}/{request.url.lstrip('/')}"
                else:
                    url = server
                
                # Prepare headers
                headers = {
                    "Accept": "application/octet-stream" if request.encoding == "ssz" else "application/json"
                }
                
                try:
                    async with session.request(
                        request.method,
                        url,
                        json=request.payload,
                        headers=headers,
                        timeout=aiohttp.ClientTimeout(total=30)
                    ) as response:
                        if response.status == 200:
                            return await response.read()
                        else:
                            error_text = await response.text()
                            print(f"HTTP {response.status} from {url}: {error_text}")
                            
                except Exception as e:
                    print(f"Request failed for {url}: {e}")
                    continue
        
        raise HTTPError(f"All servers failed for request: {request.url}")

    async def _fetch_rpc(
        self, 
        urls: List[str], 
        method: str, 
        params: List[Any], 
        as_proof: bool = False
    ) -> Union[bytes, Any]:
        """
        Fetch RPC result directly from servers
        
        Args:
            urls: List of server URLs
            method: RPC method name
            params: Method parameters
            as_proof: Whether to request proof (binary) or JSON result
            
        Returns:
            Response data (bytes if as_proof, otherwise JSON result)
            
        Raises:
            RPCError: If all servers fail
        """
        payload = {
            "id": 1,
            "jsonrpc": "2.0",
            "method": method,
            "params": params
        }
        if as_proof:
            native = _get_native()
            if native is not None and hasattr(native, "get_current_version_number"):
                payload["version"] = native.get_current_version_number()

        headers = {
            "Content-Type": "application/json",
            "Accept": "application/octet-stream" if as_proof else "application/json"
        }

        async with aiohttp.ClientSession() as session:
            for url in urls:
                try:
                    async with session.post(
                        url,
                        json=payload,
                        headers=headers,
                        timeout=aiohttp.ClientTimeout(total=30)
                    ) as response:
                        if response.status == 200:
                            if as_proof:
                                return await response.read()
                            else:
                                result = await response.json()
                                if "error" in result:
                                    raise RPCError(
                                        result["error"].get("message", "RPC error"),
                                        result["error"].get("code")
                                    )
                                return result.get("result")
                        else:
                            error_text = await response.text()
                            print(f"RPC HTTP {response.status} from {url}: {error_text}")
                            
                except Exception as e:
                    print(f"RPC request failed for {url}: {e}")
                    continue
        
        raise RPCError(f"All RPC servers failed for {method}")