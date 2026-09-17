import { readFileSync } from 'node:fs';
import { dirname, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { enrichSimulation } from '../dist/enrich.js';
import { resetCompilerStateForTests } from '../dist/compiler.js';

const TEST_DATA_DIR = resolve(dirname(fileURLToPath(import.meta.url)), 'data');

/**
 * True when `absolute` is `root` or a path inside it.
 *
 * @param root - Directory that must contain `absolute`
 * @param absolute - Resolved filesystem path
 * @return Whether `absolute` stays under `root`
 */
function isInside(root, absolute) {
    return absolute === root || absolute.startsWith(root + sep);
}

/**
 * Resolve a path relative to `test/data`.
 *
 * @param relativePath - Path under `test/data`, e.g. `state1/0xabc_sim.json`
 * @return Absolute filesystem path
 */
export function fixtureFile(relativePath) {
    const absolute = resolve(TEST_DATA_DIR, relativePath);
    if (!isInside(TEST_DATA_DIR, absolute)) {
        throw new Error(`fixture path escaped test/data: ${relativePath}`);
    }
    return absolute;
}

/**
 * Directory used as `C4_STATE_DIR` for a fixture file (`test/data/{testname}`).
 *
 * @param relativePath - Path under `test/data`
 * @return Absolute directory of the fixture's test case
 */
export function fixtureDir(relativePath) {
    const rel = relativePath.replace(/\\/g, '/');
    const slash = rel.indexOf('/');
    if (slash <= 0 || rel.startsWith('/') || rel.includes('\0')) {
        throw new Error(`fixture path must be testname/file.json, got: ${relativePath}`);
    }
    const testName = rel.slice(0, slash);
    if (testName === '.' || testName === '..') {
        throw new Error(`fixture path must be testname/file.json, got: ${relativePath}`);
    }
    const dir = resolve(TEST_DATA_DIR, testName);
    const absolute = fixtureFile(relativePath);
    if (!isInside(dir, absolute)) {
        throw new Error(`fixture path escaped test/data/${testName}: ${relativePath}`);
    }
    return dir;
}

/**
 * Reconstruct `TxParams` from the top-level simulation call.
 *
 * @param result - Simulation result (C-core JSON)
 * @return Transaction parameters for `enrichSimulation`
 */
export function txParamsFromSimulation(result) {
    const top = result?.trace?.[0];
    if (!top?.to) {
        throw new Error('simulation JSON has no trace[0].to; cannot reconstruct txParams');
    }
    return {
        to: top.to,
        from: top.from,
        data: top.input,
        value: top.value,
    };
}

/**
 * Count how many events, calls and storage slots were decoded to a name.
 *
 * Events already named by the C-core count as decoded. Calls count when
 * `enrichSimulation` attached an ABI-decoded trace entry. State changes count
 * only when the resolved slot has a `variableName` (layout match).
 *
 * @param result - Original simulation result
 * @param context - Enrichment context from `enrichSimulation`
 * @return Decoded counts plus debug summaries
 */
export function countDecoded(result, context) {
    const eventNames = [];
    const logs = result.logs ?? [];
    for (let i = 0; i < logs.length; i++) {
        const log = logs[i];
        const decoded = context.decodedEvents?.[i];
        if (decoded?.name) eventNames.push(decoded.name);
        else if (log.name) eventNames.push(log.name);
    }

    const callNames = [];
    const trace = result.trace ?? [];
    if (trace.length) {
        for (let i = 0; i < trace.length; i++) {
            const decoded = context.decodedTrace?.[i];
            if (decoded?.name) callNames.push(decoded.name);
        }
    } else if (context.decodedCall?.name) {
        callNames.push(context.decodedCall.name);
    }

    const storageNames = [];
    const unnamedStorage = [];
    for (const change of result.stateChanges ?? []) {
        const addr = change.address.toLowerCase();
        const slots = context.resolvedStorage?.get(addr) ?? [];
        const storage = change.storage ?? [];
        for (let i = 0; i < storage.length; i++) {
            const resolved = slots[i];
            if (resolved?.variableName) {
                storageNames.push(`${addr}:${resolved.variableName}`);
            } else {
                unnamedStorage.push({
                    address: change.address,
                    slot: storage[i].slot,
                    baseSlot: resolved?.baseSlot,
                });
            }
        }
    }

    return {
        events: eventNames.length,
        calls: callNames.length,
        stateChanges: storageNames.length,
        eventNames,
        callNames,
        storageNames,
        unnamedStorage,
    };
}

/**
 * Load a simulation JSON from `test/data`, point `C4_STATE_DIR` at the
 * enclosing `test/data/{testname}` directory (so Sourcify, skeleton-layout,
 * and solc artefacts are cached next to the fixture and can be checked in), run `enrichSimulation`,
 * and assert minimum decoded event / call / named-storage counts.
 *
 * @param relativePath - Path under `test/data`, e.g. `state1/0xabc_sim.json`
 * @param minEvents - Minimum decoded events (C-core names count)
 * @param minCalls - Minimum ABI-decoded trace calls
 * @param minStateChanges - Minimum storage slots resolved to a variable name
 * @param options - Optional `chainId` (default 1)
 * @return Enrichment result and counts (after the assertions pass)
 */
export async function assertEnrichedFixture(
    relativePath,
    minEvents,
    minCalls,
    minStateChanges,
    options = {},
) {
    assert.equal(Number.isInteger(minEvents) && minEvents >= 0, true, 'minEvents must be a non-negative integer');
    assert.equal(Number.isInteger(minCalls) && minCalls >= 0, true, 'minCalls must be a non-negative integer');
    assert.equal(Number.isInteger(minStateChanges) && minStateChanges >= 0, true,
        'minStateChanges must be a non-negative integer');

    const cacheDir = fixtureDir(relativePath);
    const raw = readFileSync(fixtureFile(relativePath), 'utf8');
    const result = JSON.parse(raw);
    const txParams = options.txParams ?? txParamsFromSimulation(result);
    const chainId = options.chainId ?? 1;

    const previous = process.env.C4_STATE_DIR;
    process.env.C4_STATE_DIR = cacheDir;
    let context;
    try {
        context = await enrichSimulation(result, txParams, chainId);
    } finally {
        resetCompilerStateForTests();
        if (previous === undefined) delete process.env.C4_STATE_DIR;
        else process.env.C4_STATE_DIR = previous;
    }

    const counts = countDecoded(result, context);
    const failures = [];
    if (counts.events < minEvents) {
        failures.push(`events ${counts.events} < ${minEvents} (decoded: ${fmtList(counts.eventNames)})`);
    }
    if (counts.calls < minCalls) {
        failures.push(`calls ${counts.calls} < ${minCalls} (decoded: ${fmtList(counts.callNames)})`);
    }
    if (counts.stateChanges < minStateChanges) {
        failures.push(
            `stateChanges ${counts.stateChanges} < ${minStateChanges}`
            + ` (named: ${fmtList(counts.storageNames)}; unnamed: ${fmtUnnamed(counts.unnamedStorage)})`,
        );
    }
    assert.equal(failures.join('; '), '', `enrichment shortfall for ${relativePath}: ${failures.join('; ')}`);

    return { result, txParams, context, counts, cacheDir };
}

function fmtList(names) {
    return names.length ? names.join(', ') : 'none';
}

function fmtUnnamed(entries) {
    if (!entries.length) return 'none';
    return entries.map(e => `${e.address} slot=${e.slot} base=${e.baseSlot}`).join(', ');
}
