import { ColibriClient, RequestArguments, Config, FetchRpc, MethodType } from './types.js';

function argsToArray(args: any): any[] {
    return Array.isArray(args) ? args : (args ? [args] : []);
}

function deepEqual(a: any, b: any): boolean {
    if (a === b) return true;
    if (a === null || b === null) return false;
    if (a.constructor !== b.constructor) return false;
    if (a instanceof Date) return a.getTime() === b.getTime();
    if (a instanceof RegExp) return a.source === b.source && a.flags === b.flags;
    if (a instanceof Uint8Array) return a.length === b.length && a.every((value, index) => value === b[index]);
    if (a instanceof Array) return a.length === b.length && a.every((value, index) => deepEqual(value, b[index]));
    if (a instanceof Object) return Object.keys(a).every(key => b[key] === undefined || deepEqual(a[key], b[key]));
    if (a instanceof String) return a.toLowerCase() === b.toLowerCase();
    if (a instanceof Number) return a.toFixed(10) === b.toFixed(10);
    if (a instanceof Boolean) return a === b;
    if (a instanceof BigInt) return a.toString() === b.toString();
    if (a instanceof Buffer) return a.equals(b);
    if (a instanceof Map) return a.size === b.size && Array.from(a.entries()).every(([key, value]) => deepEqual(value, b.get(key)));
    if (a instanceof Set) return a.size === b.size && Array.from(a).every(value => b.has(value));
    if (a instanceof Date) return a.getTime() === b.getTime();
    return a == b;
}

function fetch_unverified_rpc(config: Config, req: RequestArguments, fetch_rpc: FetchRpc) {
    const fallback_client = config.fallback_client;
    if (fallback_client)
        return fallback_client.request(req);

    let conf = config.chains[config.chainId as number];
    let rpcs = conf?.rpcs || config.rpcs;
    if (!rpcs || !Array.isArray(rpcs) || rpcs.length === 0)
        throw new Error("No RPC- Endpoint configured");

    return fetch_rpc(rpcs, req, false);
}


function OnlyProofStrategy(client: ColibriClient, req: RequestArguments, config: Config, fetch_rpc: FetchRpc): Promise<any> {
    return client.rpc(req.method, argsToArray(req.params));
}


async function ProofIfPossibleStrategy(client: ColibriClient, req: RequestArguments, config: Config, fetch_rpc: FetchRpc): Promise<any> {
    const method_type = await client.getMethodSupport(req.method);
    switch (method_type) {
        case MethodType.PROOFABLE:
        case MethodType.LOCAL:
            return client.rpc(req.method, argsToArray(req.params), method_type);
        default:
            return fetch_unverified_rpc(config, req, fetch_rpc)
    }
}


async function WarningStrategy(client: ColibriClient, req: RequestArguments, config: Config, fetch_rpc: FetchRpc): Promise<any> {
    const method_type = await client.getMethodSupport(req.method);
    switch (method_type) {
        case MethodType.LOCAL:
            return client.rpc(req.method, argsToArray(req.params), method_type);
        case MethodType.UNPROOFABLE:
        case MethodType.NOT_SUPPORTED:
            return fetch_unverified_rpc(config, req, fetch_rpc)
        case MethodType.PROOFABLE: {
            const [verified_result, unverified_result] = await Promise.all([
                client.rpc(req.method, argsToArray(req.params), MethodType.PROOFABLE)
                    .catch(async err => {
                        await config.warningHandler(req, `[Warning] ${req.method} failed to be verfiy: ${err.message}, falling back to Default`);
                        return undefined;
                    }),
                fetch_unverified_rpc(config, req, fetch_rpc)
            ])

            if (verified_result !== undefined && !deepEqual(verified_result, unverified_result))
                await config.warningHandler(req, `[Warning] ${req.method} does not match the rpc-result`);

            return unverified_result;
        }
    }
}


export default {
    VerifiedOnly: OnlyProofStrategy,
    VerifyIfPossible: ProofIfPossibleStrategy,
    WarningWithFallback: WarningStrategy
}