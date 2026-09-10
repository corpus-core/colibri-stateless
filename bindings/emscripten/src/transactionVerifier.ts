/**
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

import { ProviderRpcError, MethodType as C4MethodType } from './types.js';

/**
 * Transaction Verifier — defence against a compromised `eth_signTransaction` provider.
 *
 * Flow:
 * 1. Ask `fallback_provider` to sign the intended tx object
 * 2. Decode the returned raw tx with `colibri_decodeTransaction` (local)
 * 3. Compare decoded fields with the original intent (fail-closed)
 * 4. Broadcast only if they match
 *
 * `colibri_decodeTransaction` returns `EthTxData` JSON (`input`, not `data`).
 * Semantic fields omitted by the dapp default to empty/zero; the signer must
 * not fill them in. Wallet-estimated fields (`nonce`, `gas`, fees) are compared
 * only when the dapp set them.
 */

export interface TransactionVerifierConfig {
    debug?: boolean;
    chainId: number | string;
    fallback_provider?: any;
    rpcs: string[];
}

type ParseResult<T> = { ok: true; value: T } | { ok: false };

function getOwn(obj: any, key: string): { present: boolean; value: unknown } {
    if (obj == null || typeof obj !== 'object') return { present: false, value: undefined };
    if (!Object.prototype.hasOwnProperty.call(obj, key)) return { present: false, value: undefined };
    return { present: true, value: obj[key] };
}

/** Resolve RPC aliases. If several names are present they must parse to the same value. */
function resolveAliased<T>(
    obj: any,
    names: string[],
    parse: (value: unknown) => ParseResult<T>
): ParseResult<{ provided: boolean; value: T | undefined }> {
    let provided = false;
    let canonical: T | undefined;
    for (const name of names) {
        const field = getOwn(obj, name);
        if (!field.present || field.value === undefined) continue;
        const parsed = parse(field.value);
        if (!parsed.ok) return { ok: false };
        if (!provided) {
            provided = true;
            canonical = parsed.value;
        } else if (canonical !== parsed.value) {
            return { ok: false };
        }
    }
    return { ok: true, value: { provided, value: canonical } };
}

function parseQuantity(value: unknown): ParseResult<bigint> {
    if (value === undefined || value === null || value === '') return { ok: true, value: 0n };
    if (typeof value === 'boolean' || typeof value === 'object') return { ok: false };
    if (typeof value === 'bigint') return value < 0n ? { ok: false } : { ok: true, value };
    if (typeof value === 'number') {
        if (!Number.isFinite(value) || !Number.isInteger(value) || value < 0) return { ok: false };
        return { ok: true, value: BigInt(value) };
    }
    const str = String(value).trim();
    if (str === '' || str === '0x' || str === '0X') return { ok: true, value: 0n };
    if (/^0x[0-9a-fA-F]+$/.test(str)) return { ok: true, value: BigInt(str) };
    if (/^[0-9]+$/.test(str)) return { ok: true, value: BigInt(str) };
    return { ok: false };
}

/** 20-byte address, left-padded. `null`/`''`/`0x` → contract creation (`null`). */
function parseAddress(value: unknown): ParseResult<string | null> {
    if (value === undefined || value === null || value === '') return { ok: true, value: null };
    if (typeof value === 'boolean' || typeof value === 'object') return { ok: false };
    let str = String(value).trim();
    if (str === '0x' || str === '0X') return { ok: true, value: null };
    if (str.startsWith('0x') || str.startsWith('0X')) str = str.slice(2);
    if (!/^[0-9a-fA-F]+$/.test(str) || str.length > 40) return { ok: false };
    if (str.length % 2 === 1) str = '0' + str;
    return { ok: true, value: '0x' + str.toLowerCase().padStart(40, '0') };
}

function parseHexPayload(value: unknown): ParseResult<string> {
    if (value === undefined || value === null || value === '') return { ok: true, value: '' };
    if (value instanceof Uint8Array) {
        let hex = '';
        for (let i = 0; i < value.length; i++) hex += value[i].toString(16).padStart(2, '0');
        return { ok: true, value: hex };
    }
    if (typeof value === 'boolean' || typeof value === 'object') return { ok: false };
    let str = String(value).trim();
    if (str.startsWith('0x') || str.startsWith('0X')) str = str.slice(2);
    if (str === '') return { ok: true, value: '' };
    if (!/^[0-9a-fA-F]+$/.test(str)) return { ok: false };
    return { ok: true, value: str.toLowerCase() };
}

/** Exact calldata bytes. Leading zeros are significant (`0x` ≠ `0x00`). Odd nibble length is invalid. */
function parseBytes(value: unknown): ParseResult<string> {
    const hex = parseHexPayload(value);
    if (!hex.ok) return hex;
    if (hex.value.length % 2 === 1) return { ok: false };
    return { ok: true, value: '0x' + hex.value };
}

function parseHash32(value: unknown): ParseResult<string> {
    const hex = parseHexPayload(value);
    if (!hex.ok) return hex;
    if (hex.value.length > 64) return { ok: false };
    return { ok: true, value: '0x' + hex.value.padStart(64, '0') };
}

function parseYParity(value: unknown): ParseResult<bigint> {
    const q = parseQuantity(value);
    if (!q.ok) return q;
    // Accept v = 27/28 as well as yParity = 0/1.
    if (q.value === 27n || q.value === 28n) return { ok: true, value: q.value - 27n };
    if (q.value !== 0n && q.value !== 1n) return { ok: false };
    return q;
}

function warn(config: TransactionVerifierConfig, message: string): void {
    if (config.debug) console.warn(`[TransactionVerifier] ${message}`);
}

function quantitiesEqual(
    originalVal: unknown,
    decodedVal: unknown,
    defaultIfOriginalOmitted: 'zero' | 'skip'
): boolean | 'invalid' {
    const decoded = decodedVal === undefined ? { ok: true as const, value: 0n } : parseQuantity(decodedVal);
    if (!decoded.ok) return 'invalid';
    if (originalVal === undefined) {
        if (defaultIfOriginalOmitted === 'skip') return true;
        return decoded.value === 0n;
    }
    const original = parseQuantity(originalVal);
    if (!original.ok) return 'invalid';
    return original.value === decoded.value;
}

function compareAccessList(original: any, decoded: any, config: TransactionVerifierConfig): boolean {
    const origField = getOwn(original, 'accessList');
    const decField = getOwn(decoded, 'accessList');
    const origList = origField.present && origField.value != null ? origField.value : [];
    const decList = decField.present && decField.value != null ? decField.value : [];
    if (!Array.isArray(origList) || !Array.isArray(decList)) {
        warn(config, 'accessList is not an array');
        return false;
    }
    if (origList.length !== decList.length) {
        warn(config, `accessList length mismatch: original=${origList.length}, decoded=${decList.length}`);
        return false;
    }
    for (let i = 0; i < origList.length; i++) {
        const oEntry = origList[i];
        const dEntry = decList[i];
        if (oEntry == null || typeof oEntry !== 'object' || dEntry == null || typeof dEntry !== 'object') {
            warn(config, `accessList[${i}] is not an object`);
            return false;
        }
        const oa = parseAddress(getOwn(oEntry, 'address').value);
        const da = parseAddress(getOwn(dEntry, 'address').value);
        if (!oa.ok || !da.ok || oa.value === null || da.value === null || oa.value !== da.value) {
            warn(config, `accessList[${i}].address mismatch`);
            return false;
        }
        const oKeysField = getOwn(oEntry, 'storageKeys');
        const dKeysField = getOwn(dEntry, 'storageKeys');
        const oKeys = oKeysField.present && oKeysField.value != null ? oKeysField.value : [];
        const dKeys = dKeysField.present && dKeysField.value != null ? dKeysField.value : [];
        if (!Array.isArray(oKeys) || !Array.isArray(dKeys) || oKeys.length !== dKeys.length) {
            warn(config, `accessList[${i}].storageKeys mismatch`);
            return false;
        }
        for (let k = 0; k < oKeys.length; k++) {
            const okh = parseHash32(oKeys[k]);
            const dkh = parseHash32(dKeys[k]);
            if (!okh.ok || !dkh.ok || okh.value !== dkh.value) {
                warn(config, `accessList[${i}].storageKeys[${k}] mismatch`);
                return false;
            }
        }
    }
    return true;
}

function compareAuthorizationList(original: any, decoded: any, config: TransactionVerifierConfig): boolean {
    const origField = getOwn(original, 'authorizationList');
    const decField = getOwn(decoded, 'authorizationList');
    const origList = origField.present && origField.value != null ? origField.value : [];
    const decList = decField.present && decField.value != null ? decField.value : [];
    if (!Array.isArray(origList) || !Array.isArray(decList)) {
        warn(config, 'authorizationList is not an array');
        return false;
    }
    if (origList.length !== decList.length) {
        warn(config, `authorizationList length mismatch: original=${origList.length}, decoded=${decList.length}`);
        return false;
    }
    for (let i = 0; i < origList.length; i++) {
        const o = origList[i];
        const d = decList[i];
        if (o == null || typeof o !== 'object' || d == null || typeof d !== 'object') {
            warn(config, `authorizationList[${i}] is not an object`);
            return false;
        }
        const oa = parseAddress(getOwn(o, 'address').value);
        const da = parseAddress(getOwn(d, 'address').value);
        if (!oa.ok || !da.ok || oa.value === null || da.value === null || oa.value !== da.value) {
            warn(config, `authorizationList[${i}].address mismatch`);
            return false;
        }
        const origChain = getOwn(o, 'chainId');
        const expectedChain = origChain.present && origChain.value !== undefined ? origChain.value : config.chainId;
        const chainEq = quantitiesEqual(expectedChain, getOwn(d, 'chainId').value, 'skip');
        if (chainEq !== true) {
            warn(config, `authorizationList[${i}].chainId mismatch`);
            return false;
        }
        const origNonce = getOwn(o, 'nonce');
        if (origNonce.present && origNonce.value !== undefined) {
            const nonceEq = quantitiesEqual(origNonce.value, getOwn(d, 'nonce').value, 'skip');
            if (nonceEq !== true) {
                warn(config, `authorizationList[${i}].nonce mismatch`);
                return false;
            }
        }
        const origHasSig =
            (getOwn(o, 'r').present && getOwn(o, 'r').value !== undefined) ||
            (getOwn(o, 's').present && getOwn(o, 's').value !== undefined) ||
            (getOwn(o, 'yParity').present && getOwn(o, 'yParity').value !== undefined) ||
            (getOwn(o, 'v').present && getOwn(o, 'v').value !== undefined);
        if (origHasSig) {
            const or_ = parseHash32(getOwn(o, 'r').value);
            const dr = parseHash32(getOwn(d, 'r').value);
            const os = parseHash32(getOwn(o, 's').value);
            const ds = parseHash32(getOwn(d, 's').value);
            if (!or_.ok || !dr.ok || or_.value !== dr.value || !os.ok || !ds.ok || os.value !== ds.value) {
                warn(config, `authorizationList[${i}] signature mismatch`);
                return false;
            }
            const origY = getOwn(o, 'yParity').present ? getOwn(o, 'yParity').value : getOwn(o, 'v').value;
            const decY = getOwn(d, 'yParity').present ? getOwn(d, 'yParity').value : getOwn(d, 'v').value;
            const oy = parseYParity(origY);
            const dy = parseYParity(decY);
            if (!oy.ok || !dy.ok || oy.value !== dy.value) {
                warn(config, `authorizationList[${i}].yParity mismatch`);
                return false;
            }
        }
    }
    return true;
}

function compareHashList(originalVal: unknown, decodedVal: unknown, name: string, config: TransactionVerifierConfig): boolean {
    const origList = originalVal === undefined || originalVal === null ? [] : originalVal;
    const decList = decodedVal === undefined || decodedVal === null ? [] : decodedVal;
    if (!Array.isArray(origList) || !Array.isArray(decList)) {
        warn(config, `${name} is not an array`);
        return false;
    }
    if (origList.length !== decList.length) {
        warn(config, `${name} length mismatch: original=${origList.length}, decoded=${decList.length}`);
        return false;
    }
    for (let i = 0; i < origList.length; i++) {
        const oh = parseHash32(origList[i]);
        const dh = parseHash32(decList[i]);
        if (!oh.ok || !dh.ok || oh.value !== dh.value) {
            warn(config, `${name}[${i}] mismatch`);
            return false;
        }
    }
    return true;
}

/**
 * Compare an `eth_sendTransaction` object with the JSON from `colibri_decodeTransaction`.
 *
 * @param original Intended tx object from the dapp
 * @param decoded Decoded signed tx (`EthTxData`: `input`, hex quantities, `to` null if create)
 * @param config Must include the expected `chainId`
 * @return `true` only if the signed tx matches the intent
 */
export function compareTransactionParameters(original: any, decoded: any, config: TransactionVerifierConfig): boolean {
    if (config.debug) {
        console.log('[TransactionVerifier] Comparing transaction parameters:');
        console.log('Original:', original);
        console.log('Decoded:', decoded);
    }

    if (original == null || typeof original !== 'object' || decoded == null || typeof decoded !== 'object') {
        warn(config, 'original and decoded must be objects');
        return false;
    }

    const chainId = parseQuantity(config.chainId);
    if (!chainId.ok) {
        warn(config, 'config.chainId is invalid');
        return false;
    }
    const decodedChainIdField = getOwn(decoded, 'chainId');
    if (!decodedChainIdField.present) {
        warn(config, 'signed transaction has no chainId (pre-EIP-155 replay risk)');
        return false;
    }
    const decodedChainId = parseQuantity(decodedChainIdField.value);
    if (!decodedChainId.ok || decodedChainId.value !== chainId.value) {
        warn(config, `ChainId mismatch: expected=${config.chainId}, decoded=${decodedChainIdField.value}`);
        return false;
    }
    const originalChainId = getOwn(original, 'chainId');
    if (originalChainId.present && originalChainId.value !== undefined) {
        const parsed = parseQuantity(originalChainId.value);
        if (!parsed.ok || parsed.value !== chainId.value) {
            warn(config, 'original.chainId does not match config.chainId');
            return false;
        }
    }

    // Decoder maps pre-EIP-155 legacy v=27/28 to chainId 1, so a missing-chainId
    // check is not enough on mainnet. EIP-155 requires v >= 35.
    const decodedType = getOwn(decoded, 'type');
    const typeVal = decodedType.present ? parseQuantity(decodedType.value) : { ok: true as const, value: 0n };
    if (!typeVal.ok) {
        warn(config, 'invalid decoded.type');
        return false;
    }
    if (typeVal.value === 0n) {
        const vField = getOwn(decoded, 'v');
        const v = vField.present ? parseQuantity(vField.value) : { ok: false as const };
        if (!v.ok || v.value < 35n) {
            warn(config, 'unprotected pre-EIP-155 signature');
            return false;
        }
    }

    const origTo = resolveAliased(original, ['to'], parseAddress);
    const decTo = resolveAliased(decoded, ['to'], parseAddress);
    if (!origTo.ok || !decTo.ok) {
        warn(config, 'invalid to address');
        return false;
    }
    const expectedTo = origTo.value.provided ? origTo.value.value : null;
    const actualTo = decTo.value.provided ? decTo.value.value : null;
    if (expectedTo !== actualTo) {
        warn(config, `to mismatch: original='${expectedTo}', decoded='${actualTo}'`);
        return false;
    }

    const origFrom = resolveAliased(original, ['from'], parseAddress);
    const decFrom = resolveAliased(decoded, ['from'], parseAddress);
    if (!origFrom.ok || !decFrom.ok) {
        warn(config, 'invalid from address');
        return false;
    }
    if (origFrom.value.provided && origFrom.value.value !== (decFrom.value.provided ? decFrom.value.value : null)) {
        warn(config, `from mismatch: original='${origFrom.value.value}', decoded='${decFrom.value.value}'`);
        return false;
    }

    const origValue = getOwn(original, 'value');
    const valueEq = quantitiesEqual(
        origValue.present ? origValue.value : undefined,
        getOwn(decoded, 'value').value,
        'zero'
    );
    if (valueEq !== true) {
        warn(config, `value mismatch: original='${origValue.value}', decoded='${decoded.value}'`);
        return false;
    }

    const origData = resolveAliased(original, ['data', 'input'], parseBytes);
    const decData = resolveAliased(decoded, ['input', 'data'], parseBytes);
    if (!origData.ok || !decData.ok) {
        warn(config, 'invalid calldata (data/input)');
        return false;
    }
    const expectedData = origData.value.provided ? origData.value.value : '0x';
    const actualData = decData.value.provided ? decData.value.value : '0x';
    if (expectedData !== actualData) {
        warn(config, `data/input mismatch: original='${expectedData}', decoded='${actualData}'`);
        return false;
    }

    const origType = getOwn(original, 'type');
    if (origType.present && origType.value !== undefined) {
        const typeEq = quantitiesEqual(origType.value, decoded.type, 'skip');
        if (typeEq !== true) {
            warn(config, `type mismatch: original='${origType.value}', decoded='${decoded.type}'`);
            return false;
        }
    }

    const origNonce = getOwn(original, 'nonce');
    if (origNonce.present && origNonce.value !== undefined) {
        const nonceEq = quantitiesEqual(origNonce.value, decoded.nonce, 'skip');
        if (nonceEq !== true) {
            warn(config, `nonce mismatch: original='${origNonce.value}', decoded='${decoded.nonce}'`);
            return false;
        }
    }

    const origGas = resolveAliased(original, ['gas', 'gasLimit'], parseQuantity);
    if (!origGas.ok) {
        warn(config, 'invalid gas/gasLimit');
        return false;
    }
    if (origGas.value.provided) {
        const gasEq = quantitiesEqual(origGas.value.value, decoded.gas, 'skip');
        if (gasEq !== true) {
            warn(config, `gas mismatch: original='${origGas.value.value}', decoded='${decoded.gas}'`);
            return false;
        }
    }

    const origMaxFee = getOwn(original, 'maxFeePerGas');
    const origPrio = getOwn(original, 'maxPriorityFeePerGas');
    const origGasPrice = getOwn(original, 'gasPrice');
    const isEip1559 = origMaxFee.present || origPrio.present;

    if (origMaxFee.present && origMaxFee.value !== undefined) {
        const eq = quantitiesEqual(origMaxFee.value, decoded.maxFeePerGas, 'skip');
        if (eq !== true) {
            warn(config, `maxFeePerGas mismatch`);
            return false;
        }
    }
    if (origPrio.present && origPrio.value !== undefined) {
        const eq = quantitiesEqual(origPrio.value, decoded.maxPriorityFeePerGas, 'skip');
        if (eq !== true) {
            warn(config, `maxPriorityFeePerGas mismatch`);
            return false;
        }
    }
    // Decoded EIP-1559 txs still carry an *effective* gasPrice; do not compare it.
    if (origGasPrice.present && origGasPrice.value !== undefined && !isEip1559) {
        const eq = quantitiesEqual(origGasPrice.value, decoded.gasPrice, 'skip');
        if (eq !== true) {
            warn(config, `gasPrice mismatch`);
            return false;
        }
    }

    if (!compareAccessList(original, decoded, config)) return false;
    if (!compareAuthorizationList(original, decoded, config)) return false;

    const origBlobs = getOwn(original, 'blobVersionedHashes');
    const decBlobs = getOwn(decoded, 'blobVersionedHashes');
    if (!compareHashList(
        origBlobs.present ? origBlobs.value : [],
        decBlobs.present ? decBlobs.value : [],
        'blobVersionedHashes',
        config
    )) return false;

    const origBlobFee = getOwn(original, 'maxFeePerBlobGas');
    if (origBlobFee.present && origBlobFee.value !== undefined) {
        const eq = quantitiesEqual(origBlobFee.value, decoded.maxFeePerBlobGas, 'skip');
        if (eq !== true) {
            warn(config, `maxFeePerBlobGas mismatch`);
            return false;
        }
    }

    const origMint = getOwn(original, 'mint');
    const mintEq = quantitiesEqual(
        origMint.present ? origMint.value : undefined,
        getOwn(decoded, 'mint').present ? decoded.mint : 0n,
        'zero'
    );
    if (mintEq !== true) {
        warn(config, 'unexpected mint field on signed transaction');
        return false;
    }

    const origSource = getOwn(original, 'sourceHash');
    const decSource = getOwn(decoded, 'sourceHash');
    if (decSource.present) {
        const expected = origSource.present ? origSource.value : '0x' + '00'.repeat(32);
        const oh = parseHash32(expected);
        const dh = parseHash32(decSource.value);
        if (!oh.ok || !dh.ok || oh.value !== dh.value) {
            warn(config, 'unexpected sourceHash on signed transaction');
            return false;
        }
    }

    const origSys = getOwn(original, 'isSystemTx');
    const decSys = getOwn(decoded, 'isSystemTx');
    const expectedSys = origSys.present ? Boolean(origSys.value) : false;
    const actualSys = decSys.present ? Boolean(decSys.value) : false;
    if (expectedSys !== actualSys) {
        warn(config, 'unexpected isSystemTx on signed transaction');
        return false;
    }

    return true;
}

export class TransactionVerifier {

    /**
     * Verifizierte Ausführung von eth_sendTransaction
     * 1. Transaction über fallbackProvider signieren lassen
     * 2. Signierte Transaction mit colibri_decodeTransaction dekodieren
     * 3. Dekodierte Parameter mit Original vergleichen
     * 4. eth_sendRawTransaction ausführen
     */
    static async verifyAndSendTransaction(
        txObject: any,
        config: TransactionVerifierConfig,
        rpcMethod: (method: string, args: any[], method_type?: C4MethodType) => Promise<any>,
        fetchRpc: (urls: string[], payload: any, as_proof?: boolean, fetchFn?: typeof globalThis.fetch) => Promise<any>,
        fetchFn?: typeof globalThis.fetch
    ): Promise<string> {
        if (config.debug) console.log('[TransactionVerifier] Verifying transaction before sending:', txObject);

        if (!config.fallback_provider) {
            throw new ProviderRpcError(4203, 'Transaction verification requires fallback_provider to be configured');
        }

        try {
            // 1. Transaction über fallbackProvider signieren lassen
            const signedRawTx = await config.fallback_provider.request({
                method: 'eth_signTransaction',
                params: [txObject]
            }) as string;

            // 2. Signierte Transaction dekodieren
            const decodedTx = await rpcMethod('colibri_decodeTransaction', [signedRawTx], C4MethodType.LOCAL);

            // 3. Dekodierte Parameter mit Original vergleichen
            const isValid = compareTransactionParameters(txObject, decodedTx, config);
            if (!isValid) {
                throw new ProviderRpcError(4201, 'Signed transaction does not match original parameters');
            }

            // 4. Verifizierte Transaction senden
            return await fetchRpc(config.rpcs, {
                method: 'eth_sendRawTransaction',
                params: [signedRawTx]
            }, false, fetchFn);

        } catch (error: any) {
            if (config.debug) console.error('[TransactionVerifier] Transaction verification failed:', error);
            throw new ProviderRpcError(4202, `Transaction verification failed: ${error.message}`);
        }
    }

}

/**
 * Prototype Protection - Schutz vor NPM Supply-Chain-Attacks
 * 
 * Friert kritische Prototypen und Methoden ein, um Manipulation zu verhindern
 */
export class PrototypeProtection {

    /**
     * Schützt eine Klasse vor Prototype Pollution
     */
    static protectClass<T>(targetClass: new (...args: any[]) => T, criticalMethods: readonly string[] = []) {
        // Einfrieren des Prototypes
        Object.freeze(targetClass.prototype);

        // Kritische Methoden gegen Überschreibung schützen
        criticalMethods.forEach(method => {
            const descriptor = Object.getOwnPropertyDescriptor(targetClass.prototype, method);
            if (descriptor) {
                Object.defineProperty(targetClass.prototype, method, {
                    ...descriptor,
                    writable: false,
                    configurable: false
                });
            }
        });
    }

    /**
     * Schützt ein Konfigurationsobjekt vor Manipulation
     */
    static protectConfig(config: any, criticalArrays: string[] = []) {
        // Kritische Arrays einfrieren
        criticalArrays.forEach(arrayName => {
            if (config[arrayName] && Array.isArray(config[arrayName])) {
                Object.freeze(config[arrayName]);
            }
        });
    }
}

/**
 * Transaction Utilities - Hilfsfunktionen für Transaction-Handling
 */
export class TransactionUtils {

    /**
     * Validiert ein Transaction-Objekt
     */
    static validateTransactionObject(txObject: any): boolean {
        if (!txObject || typeof txObject !== 'object') return false;

        // Mindestanforderungen
        if (!txObject.to && !txObject.data) return false; // Entweder to-Adresse oder data für Contract-Creation

        return true;
    }

    /**
     * Bereinigt Transaction-Parameter für verschiedene Standards
     */
    static cleanupTransactionParams(txObject: any): any {
        const cleaned = { ...txObject };

        // EIP-1559 vs Legacy Gas-Handling
        if (cleaned.maxFeePerGas || cleaned.maxPriorityFeePerGas) {
            // EIP-1559 - gasPrice entfernen
            delete cleaned.gasPrice;
        } else if (cleaned.gasPrice) {
            // Legacy - EIP-1559 Felder entfernen
            delete cleaned.maxFeePerGas;
            delete cleaned.maxPriorityFeePerGas;
        }

        return cleaned;
    }

    /**
     * Erstellt eine Zusammenfassung einer Transaction für Logging
     */
    static summarizeTransaction(txObject: any): string {
        const to = txObject.to || 'Contract Creation';
        const value = txObject.value ? `${parseInt(txObject.value, 16)} wei` : '0 wei';
        const gas = txObject.gas ? parseInt(txObject.gas, 16) : 'auto';

        return `Transaction(to: ${to}, value: ${value}, gas: ${gas})`;
    }
}

// Export aller Klassen als Default
export default {
    TransactionVerifier,
    PrototypeProtection,
    TransactionUtils,
    compareTransactionParameters
};
