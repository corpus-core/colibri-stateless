import { EventEmitter } from './eventEmitter.js';
import { ProviderRpcError, ProviderConnectInfo } from './types.js';

// Helper function for chain ID formatting - intentionally kept separate for now
// If this function is *only* used by ConnectionState, it could be moved inside or made local.
// However, C4Client.rpc also uses it for its LOCAL eth_chainId case, so it might need broader access.
// For this refactoring, we assume it's passed in or globally accessible if needed elsewhere.

// Helper function for chain ID formatting (can be used by ConnectionManager and C4Client)
function formatChainId(value: any, debug?: boolean): string | null {
    if (typeof value === 'string') {
        if (value.startsWith('0x')) {
            return value.toLowerCase();
        }
        const parsed = parseInt(value, 10);
        if (!isNaN(parsed)) {
            return '0x' + parsed.toString(16);
        }
    } else if (typeof value === 'number') {
        return '0x' + value.toString(16);
    }
    if (debug) {
        console.warn('Could not format chainId:', value);
    }
    return null;
}


interface ConnectionStateConfig {
    chainId: number; // The configured default chainId
    debug?: boolean;
}

export class ConnectionState {
    private _isConnected: boolean = false;
    private _currentChainId: string | null = null;
    private _initialConnectionAttempted: boolean = false;

    private config: ConnectionStateConfig;
    private fetchRpcChainIdCallback: () => Promise<any>;
    private eventEmitter: EventEmitter;

    constructor(
        config: ConnectionStateConfig,
        fetchRpcChainIdCallback: () => Promise<any>,
        eventEmitter: EventEmitter
    ) {
        this.config = config;
        this.fetchRpcChainIdCallback = fetchRpcChainIdCallback;
        this.eventEmitter = eventEmitter;
        this._currentChainId = formatChainId(this.config.chainId, this.config.debug);
    }

    public get isConnected(): boolean {
        return this._isConnected;
    }

    public get currentChainId(): string | null {
        return this._currentChainId;
    }

    public get initialConnectionAttempted(): boolean {
        return this._initialConnectionAttempted;
    }

    public async attemptInitialConnection(): Promise<void> {
        if (this._initialConnectionAttempted) return;
        this._initialConnectionAttempted = true;

        if (this.config.debug) {
            console.log('[CS] Attempting initial connection...');
        }

        try {
            const result = await this.fetchRpcChainIdCallback();
            const formattedChainId = formatChainId(result, this.config.debug);

            if (formattedChainId) {
                this._isConnected = true;
                this._currentChainId = formattedChainId;
                if (this.config.debug) {
                    console.log('[CS] Initial connection successful. ChainId:', this._currentChainId);
                }
                this.eventEmitter.emit('connect', { chainId: this._currentChainId } as ProviderConnectInfo);
            } else {
                this._isConnected = false;
                if (this.config.debug) {
                    console.warn('[CS] Initial eth_chainId did not return a valid format or was null:', result, ". Client remains disconnected but uses configured chainId.");
                }
            }
        } catch (error) {
            this._isConnected = false;
            if (this.config.debug) {
                console.error('[CS] Initial connection check failed:', error);
                if (error instanceof ProviderRpcError) {
                    console.error('[CS] ProviderRpcError during initial connection check:', error.code, error.message);
                }
            }
        }
    }

    public processSuccessfulRequest(requestMethod: string, requestResult: any): void {
        if (!this._isConnected) {
            this._isConnected = true;

            const updateAndEmitConnect = async () => {
                let determinedChainId: string | null = null;
                if (requestMethod === 'eth_chainId') {
                    determinedChainId = formatChainId(requestResult, this.config.debug);
                } else {
                    try {
                        const chainIdResult = await this.fetchRpcChainIdCallback();
                        determinedChainId = formatChainId(chainIdResult, this.config.debug);
                    } catch (e) {
                        if (this.config.debug) console.warn('[CS] Failed to fetch chainId for connect event, using current _currentChainId as fallback.', e);
                        determinedChainId = this._currentChainId;
                    }
                }
                this._currentChainId = determinedChainId || formatChainId(this.config.chainId, this.config.debug);
                this.eventEmitter.emit('connect', { chainId: this._currentChainId } as ProviderConnectInfo);
            };

            updateAndEmitConnect().catch(err => {
                if (this.config.debug) console.error("[CS] Error during background processing for connect event:", err);
            });
        } else if (requestMethod === 'eth_chainId') {
            const newChainId = formatChainId(requestResult, this.config.debug);
            if (newChainId && this._currentChainId !== newChainId) {
                const oldChainId = this._currentChainId;
                this._currentChainId = newChainId;
                if (this.config.debug) {
                    console.log('[CS] ChainId changed from', oldChainId, 'to', this._currentChainId);
                }
                this.eventEmitter.emit('chainChanged', this._currentChainId);
            }
        }
    }

    public processFailedRequest(error: ProviderRpcError): void {
        if (error.code === 4900 && this._isConnected) {
            this._isConnected = false;
            if (this.config.debug) {
                console.log('[CS] Disconnected due to error 4900.');
            }
            this.eventEmitter.emit('disconnect', error);
        }
    }
} 