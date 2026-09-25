/**
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

import { AbiCoder } from 'ethers';
import type {
    AddressLabel, ContractMetadata, EnrichedContext, LabelTable,
    SimulationResult, TxParams,
} from './types.js';
import { lookupAddress } from './known_addresses.js';
import { shortenAddress } from './format.js';

/** Hex calldata for `name()`, `symbol()` and `decimals()`. */
const NAME_CALL = '0x06fdde03';
const SYMBOL_CALL = '0x95d89b41';
const DECIMALS_CALL = '0x313ce567';

const AMOUNT_EVENTS = new Set(['Transfer', 'Approval', 'Deposit', 'Withdrawal']);
const AMOUNT_VARS = new Set([
    'balanceOf', '_balances', 'balances', 'totalSupply', '_totalSupply', 'allowance', '_allowances',
]);

/** Injected verified call. Return hex data, or `null` on failure. */
export type EthCallFn = (to: string, data: string) => Promise<string | null>;

/**
 * Build the label table for one transaction.
 *
 * Provenance order for the name: signer, curated list, verified `contractName`,
 * then `name()` / `symbol()` when `ethCall` is set. Decimals come from the
 * curated list, else `decimals()`. Addresses with none of these stay unlabeled.
 *
 * @param result - Simulation result
 * @param txParams - Original transaction parameters
 * @param options - Contract metadata and optional `eth_call`
 * @return Label table keyed by lowercase address
 */
export async function resolveLabels(
    result: SimulationResult,
    txParams: TxParams,
    options?: {
        contracts?: Map<string, ContractMetadata>;
        ethCall?: EthCallFn;
        resolvedStorage?: EnrichedContext['resolvedStorage'];
        decodedEvents?: ({ name?: string } | null)[];
    },
): Promise<LabelTable> {
    const drafts = draftLabels(result, txParams, options?.contracts);
    if (options?.ethCall) {
        const candidates = tokenCandidates(result, options.resolvedStorage, options.decodedEvents);
        for (const addr of candidates) {
            let draft = drafts.byAddress[addr];
            if (draft?.provenance === 'signer') continue;
            if (draft?.provenance === 'known') {
                if (draft.decimals == null) await fillDecimals(draft, addr, options.ethCall);
                continue;
            }
            if (!draft) {
                const declared = await readSelfDeclared(addr, options.ethCall);
                if (declared) {
                    drafts.byAddress[addr] = declared;
                    drafts.order.push(addr);
                    draft = declared;
                }
            }
            if (draft && draft.decimals == null) await fillDecimals(draft, addr, options.ethCall);
        }
    }
    return finish(drafts);
}

/**
 * Labels from curated data, the signer and verified source names.
 * No chain calls. Used when the prompt is built synchronously.
 *
 * @param result - Simulation result
 * @param txParams - Original transaction parameters
 * @param contracts - Enriched metadata, optional
 * @return Label table
 */
export function buildLabelTableSync(
    result: SimulationResult,
    txParams: TxParams,
    contracts?: Map<string, ContractMetadata>,
): LabelTable {
    return finish(draftLabels(result, txParams, contracts));
}

interface Drafts {
    order: string[];
    byAddress: Record<string, AddressLabel>;
}

function draftLabels(
    result: SimulationResult,
    txParams: TxParams,
    contracts?: Map<string, ContractMetadata>,
): Drafts {
    const order: string[] = [];
    const byAddress: Record<string, AddressLabel> = {};
    const signer = txParams.from?.toLowerCase();

    const add = (address?: string): void => {
        if (!address || !address.startsWith('0x')) return;
        const addr = address.toLowerCase();
        if (byAddress[addr]) return;
        const label = draftOne(addr, signer, contracts?.get(addr)?.contractName);
        if (!label) return;
        byAddress[addr] = label;
        order.push(addr);
    };

    add(txParams.from);
    add(txParams.to);
    for (const log of result.logs ?? []) add(log.raw?.address);
    for (const change of result.stateChanges ?? []) add(change.address);
    for (const frame of result.trace ?? []) {
        add(frame.from);
        add(frame.to);
    }
    return { order, byAddress };
}

function draftOne(addr: string, signer: string | undefined, contractName: string | null | undefined): AddressLabel | undefined {
    if (signer && addr === signer) {
        return {
            address: addr,
            provenance: 'signer',
            name: 'you',
            text: `you (${shortenAddress(addr)})`,
        };
    }
    const known = lookupAddress(addr);
    if (known) {
        return {
            address: addr,
            provenance: 'known',
            name: known.label,
            text: render('known', known.label, addr),
            symbol: known.symbol,
            decimals: known.decimals,
            decimalsSource: known.decimals != null ? 'known' : undefined,
        };
    }
    const sourceName = sanitizeName(contractName);
    if (sourceName) {
        return {
            address: addr,
            provenance: 'source',
            name: sourceName,
            text: render('source', sourceName, addr),
        };
    }
    return undefined;
}

function tokenCandidates(
    result: SimulationResult,
    resolvedStorage?: EnrichedContext['resolvedStorage'],
    decodedEvents?: ({ name?: string } | null)[],
): Set<string> {
    const out = new Set<string>();
    for (let i = 0; i < (result.logs ?? []).length; i++) {
        const log = result.logs[i];
        const name = log.name ?? decodedEvents?.[i]?.name;
        if (name && AMOUNT_EVENTS.has(name) && log.raw?.address) {
            out.add(log.raw.address.toLowerCase());
        }
    }
    if (!resolvedStorage) return out;
    for (const [addr, slots] of resolvedStorage) {
        for (const slot of slots) {
            if (isAmountVariable(slot.variableName) || slot.members?.some(m => isAmountVariable(m.variableName))) {
                out.add(addr.toLowerCase());
            }
        }
    }
    return out;
}

/** True when a storage variable holds a token amount. */
export function isAmountVariable(name: string | undefined): boolean {
    if (!name) return false;
    const base = name.split(/[.[]/)[0];
    return AMOUNT_VARS.has(base);
}

async function readSelfDeclared(addr: string, ethCall: EthCallFn): Promise<AddressLabel | undefined> {
    const name = sanitizeName(await readString(ethCall, addr, NAME_CALL));
    const symbol = sanitizeName(await readString(ethCall, addr, SYMBOL_CALL));
    const display = name || symbol;
    if (!display) return undefined;
    return {
        address: addr,
        provenance: 'self-declared',
        name: display,
        text: render('self-declared', display, addr),
        symbol,
    };
}

async function fillDecimals(draft: AddressLabel | undefined, addr: string, ethCall: EthCallFn): Promise<void> {
    if (!draft || draft.decimals != null) return;
    const raw = await ethCall(addr, DECIMALS_CALL);
    const decimals = decodeUint(raw);
    if (decimals == null || decimals > 36) return;
    draft.decimals = decimals;
    draft.decimalsSource = 'call';
}

async function readString(ethCall: EthCallFn, addr: string, data: string): Promise<string | undefined> {
    const raw = await ethCall(addr, data);
    return decodeString(raw);
}

function finish(drafts: Drafts): LabelTable {
    const groups = new Map<string, AddressLabel[]>();
    for (const addr of drafts.order) {
        const label = drafts.byAddress[addr];
        if (!label || label.provenance === 'signer') continue;
        const list = groups.get(label.name) ?? [];
        list.push(label);
        groups.set(label.name, list);
    }
    for (const list of groups.values()) {
        if (list.length < 2) continue;
        list.forEach((label, i) => {
            label.name = `${label.name}#${i + 1}`;
            label.text = render(label.provenance, label.name, label.address);
        });
    }
    return { byAddress: drafts.byAddress };
}

function render(provenance: AddressLabel['provenance'], name: string, addr: string): string {
    const short = shortenAddress(addr);
    if (provenance === 'signer') return `you (${short})`;
    if (provenance === 'source') return `${name} (${short}, from source)`;
    if (provenance === 'self-declared') return `"${name}" (${short}, self-declared)`;
    return `${name} (${short})`;
}

/**
 * Look up the prompt text for an address.
 *
 * @param labels - Table from `resolveLabels` or `buildLabelTableSync`
 * @param address - Any casing
 * @return Label text, or the shortened hex when the address has no provenance
 */
export function labelText(labels: LabelTable, address: string): string {
    const hit = labels.byAddress[address.toLowerCase()];
    if (hit) return hit.text;
    const known = lookupAddress(address);
    return known ? `${known.label} (${shortenAddress(address)})` : shortenAddress(address);
}

/** Drop characters that would break a one-line prompt or impersonate provenance marks. */
function sanitizeName(name: string | null | undefined): string | undefined {
    if (!name) return undefined;
    const cleaned = name.replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029"\\]/g, '').trim().slice(0, 64);
    if (!cleaned) return undefined;
    if (/from source|self-declared/i.test(cleaned)) return undefined;
    return cleaned;
}

function decodeString(data: string | null): string | undefined {
    if (!data || data === '0x' || data.length < 2) return undefined;
    const coder = AbiCoder.defaultAbiCoder();
    try {
        const [value] = coder.decode(['string'], data);
        if (typeof value === 'string' && value.trim()) return value;
    } catch { /* bytes32 tokens */ }
    try {
        const [word] = coder.decode(['bytes32'], data);
        const hex = String(word).slice(2);
        let text = '';
        for (let i = 0; i < hex.length; i += 2) {
            const byte = parseInt(hex.slice(i, i + 2), 16);
            if (!byte) break;
            text += String.fromCharCode(byte);
        }
        return text || undefined;
    } catch {
        return undefined;
    }
}

function decodeUint(data: string | null): number | undefined {
    if (!data || data === '0x') return undefined;
    try {
        const [value] = AbiCoder.defaultAbiCoder().decode(['uint8'], data);
        const n = Number(value);
        return Number.isInteger(n) ? n : undefined;
    } catch {
        return undefined;
    }
}
