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
    ProofError,
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
        trusted_block_hashes: List[str] = None,
        include_code: bool = False,
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
            trusted_block_hashes: List of trusted block hashes for verification
            include_code: Whether to include code in proofs
            storage: Storage implementation (defaults to DefaultStorage)
            request_handler: Optional request handler for testing
        """
        self.chain_id = chain_id
        # Fix Python falsy-array bug: [] or default returns default!
        self.provers = provers if provers is not None else self._get_default_provers(chain_id)
        self.eth_rpcs = eth_rpcs if eth_rpcs is not None else self._get_default_eth_rpcs(chain_id)
        self.beacon_apis = beacon_apis if beacon_apis is not None else self._get_default_beacon_apis(chain_id)
        self.checkpointz = checkpointz if checkpointz is not None else self._get_default_checkpointz(chain_id)
        self.trusted_block_hashes = trusted_block_hashes if trusted_block_hashes is not None else []
        self.include_code = include_code
        self.request_handler = request_handler

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
            1: ["https://sync-mainnet.beaconcha.in", "https://beaconstate.info", "https://sync.invis.tools", "https://beaconstate.ethstaker.cc"],
            11155111: [],  # No public checkpointz for Sepolia yet
            100: [],  # TODO: Add Gnosis checkpointz servers
            10200: [],  # No public checkpointz for Chiado yet
        }
        return defaults.get(chain_id, [])

    def get_method_support(self, method: str) -> MethodType:
        """
        Check what type of support a method has
        
        Args:
            method: RPC method name
            
        Returns:
            MethodType indicating the support level
        """
        native = _get_native()
        if native and hasattr(native, 'get_method_support'):
            try:
                type_int = native.get_method_support(self.chain_id, method)
                return MethodType(type_int)
            except (ValueError, TypeError):
                return MethodType.UNKNOWN
        
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
            return MethodType.NOT_SUPPORTED

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
            ctx = native.create_prover_ctx(
                method, 
                params_json, 
                self.chain_id, 
                1 if self.include_code else 0
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
            trusted_hashes_json = json.dumps(self.trusted_block_hashes)
            
            ctx = native.create_verify_ctx(
                proof, 
                method, 
                params_json, 
                self.chain_id, 
                trusted_hashes_json
            )
            
            if not ctx:
                raise VerificationError(f"Failed to create verification context for {method}")

            try:
                # Execute verification with request handling
                while True:
                    status_json = native.verify_execute_json_status(ctx)
                    if not status_json:
                        raise VerificationError("Verification execution returned null")
                    
                    # Debug: Print the raw JSON response from C
                    # DEBUG: Raw C JSON response available for debugging
                    
                    try:
                        status = json.loads(status_json)
                    except json.JSONDecodeError as e:
                        # Workaround: Try to fix trailing comma issue from C
                        if "," in status_json and status_json.rstrip().endswith(",}"):
                            # Workaround: Fix trailing comma in C JSON
                            fixed_json = status_json.replace(",}", "}")
                            print(f"   Fixed JSON: {repr(fixed_json)}")
                            try:
                                status = json.loads(fixed_json)
                                # JSON parsing successful after fix
                            except json.JSONDecodeError as e2:
                                print(f"Still invalid after fix: {e2}")
                                raise VerificationError(f"Invalid JSON in verification response: {e2}")
                        else:
                            # JSON parse error occurred
                            print(f"   Raw response: {status_json}")
                            raise VerificationError(f"Invalid JSON in verification response: {e}")
                    
                    if status["status"] == "success":
                        return status.get("result")
                    elif status["status"] == "error":
                        raise VerificationError(status.get("error", "Unknown verification error"))
                    elif status["status"] == "pending":
                        await self._handle_requests(status.get("requests", []), use_prover_fallback=True)
                    else:
                        raise VerificationError(f"Unknown status: {status['status']}")
            
            finally:
                native.verify_free_ctx(ctx)
                
        except json.JSONDecodeError as e:
            raise VerificationError(f"Invalid JSON in verification response: {e}") from e
        except Exception as e:
            if isinstance(e, VerificationError):
                raise
            raise VerificationError(f"Proof verification failed: {e}") from e

    async def rpc(self, method: str, params: List[Any]) -> Any:
        """
        Execute an RPC call with automatic proof handling
        
        Args:
            method: RPC method name
            params: Method parameters
            
        Returns:
            RPC result
            
        Raises:
            ColibriError: If the RPC call fails
        """
        method_type = self.get_method_support(method)
        
        if method_type == MethodType.PROOFABLE:
            # Try to fetch proof from prover first
            if self.provers:
                try:
                    proof = await self._fetch_rpc(self.provers, method, params, as_proof=True)
                except Exception:
                    # Fallback to local proof creation
                    proof = await self.create_proof(method, params)
            else:
                proof = await self.create_proof(method, params)
            
            return await self.verify_proof(proof, method, params)
            
        elif method_type == MethodType.UNPROOFABLE:
            return await self._fetch_rpc(self.eth_rpcs, method, params, as_proof=False)
            
        elif method_type == MethodType.LOCAL:
            # Local methods use empty proof
            return await self.verify_proof(b"", method, params)
            
        elif method_type == MethodType.NOT_SUPPORTED:
            raise ColibriError(f"Method {method} is not supported")
            
        else:
            raise ColibriError(f"Unknown method type for {method}")

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
                    servers = self.checkpointz
                elif request.request_type == "beacon_api":
                    if use_prover_fallback and self.provers:
                        servers = self.provers
                    else:
                        servers = self.beacon_apis
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