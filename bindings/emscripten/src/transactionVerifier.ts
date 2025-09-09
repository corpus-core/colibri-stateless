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

import { ProviderRpcError, Config as C4Config, MethodType as C4MethodType } from './types.js';

/**
 * Transaction Verifier - Sichere Verifikation von eth_sendTransaction
 * 
 * Implementiert eine Sicherheitsschicht gegen NPM Supply-Chain-Attacks:
 * 1. Transaction über fallbackProvider signieren lassen
 * 2. Signierte Transaction mit eth_decodeTransaction dekodieren  
 * 3. Dekodierte Parameter mit Original vergleichen
 * 4. Bei Übereinstimmung: eth_sendRawTransaction ausführen
 */

export interface TransactionVerifierConfig {
    debug?: boolean;
    chainId: number | string;
    fallback_provider?: any;
    rpcs: string[];
}

export class TransactionVerifier {

    /**
     * Verifizierte Ausführung von eth_sendTransaction
     * 1. Transaction über fallbackProvider signieren lassen
     * 2. Signierte Transaction mit eth_decodeTransaction dekodieren
     * 3. Dekodierte Parameter mit Original vergleichen
     * 4. eth_sendRawTransaction ausführen
     */
    static async verifyAndSendTransaction(
        txObject: any,
        config: TransactionVerifierConfig,
        rpcMethod: (method: string, args: any[], method_type?: C4MethodType) => Promise<any>,
        fetchRpc: (urls: string[], payload: any, as_proof?: boolean) => Promise<any>
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
            const decodedTx = await rpcMethod('eth_decodeTransaction', [signedRawTx], C4MethodType.LOCAL);

            // 3. Dekodierte Parameter mit Original vergleichen
            const isValid = this.compareTransactionParameters(txObject, decodedTx, config);
            if (!isValid) {
                throw new ProviderRpcError(4201, 'Signed transaction does not match original parameters');
            }

            // 4. Verifizierte Transaction senden
            return await fetchRpc(config.rpcs, {
                method: 'eth_sendRawTransaction',
                params: [signedRawTx]
            }, false);

        } catch (error: any) {
            if (config.debug) console.error('[TransactionVerifier] Transaction verification failed:', error);
            throw new ProviderRpcError(4202, `Transaction verification failed: ${error.message}`);
        }
    }

    /**
     * Vergleicht die Parameter einer ursprünglichen Transaction mit einer dekodierten signierten Transaction
     */
    private static compareTransactionParameters(original: any, decoded: any, config: TransactionVerifierConfig): boolean {
        if (config.debug) {
            console.log('[TransactionVerifier] Comparing transaction parameters:');
            console.log('Original:', original);
            console.log('Decoded:', decoded);
        }

        // Kritische Parameter vergleichen
        const criticalFields = ['to', 'value', 'data', 'gas', 'gasPrice', 'maxFeePerGas', 'maxPriorityFeePerGas', 'nonce'];

        for (const field of criticalFields) {
            if (original[field] !== undefined) {
                // Normalisierung für Hex-Werte (0x-Prefix, Leading Zeros)
                const originalValue = this.normalizeHexValue(original[field]);
                const decodedValue = this.normalizeHexValue(decoded[field]);

                if (originalValue !== decodedValue) {
                    if (config.debug) {
                        console.warn(`[TransactionVerifier] Transaction parameter mismatch in '${field}': original='${originalValue}', decoded='${decodedValue}'`);
                    }
                    return false;
                }
            }
        }

        // ChainId prüfen
        if (decoded.chainId && parseInt(decoded.chainId, 16) !== parseInt(config.chainId as string)) {
            if (config.debug) {
                console.warn(`[TransactionVerifier] ChainId mismatch: expected=${config.chainId}, decoded=${parseInt(decoded.chainId, 16)}`);
            }
            return false;
        }

        return true;
    }

    /**
     * Normalisiert Hex-Werte für Vergleiche (entfernt führende Nullen, stellt 0x-Prefix sicher)
     */
    private static normalizeHexValue(value: any): string {
        if (value === undefined || value === null) return '';
        if (typeof value === 'number') return '0x' + value.toString(16);

        const str = value.toString();
        if (!str.startsWith('0x')) return '0x' + str;

        // Führende Nullen entfernen, aber mindestens eine Ziffer behalten
        const withoutPrefix = str.slice(2);
        const normalized = withoutPrefix.replace(/^0+/, '') || '0';
        return '0x' + normalized;
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
    TransactionUtils
};
