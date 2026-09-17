import { after, describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { WETH_DEPOSIT_RESULT, TX_PARAMS } from './fixtures.mjs';
import {
    assertEnrichedFixture, countDecoded, fixtureDir, fixtureFile, txParamsFromSimulation,
} from './replay.mjs';

const TEMP_FIXTURE = '_replay_unit/sim.json';

/**
 * Write a JSON fixture under `test/data` for helper tests that must go through
 * `assertEnrichedFixture` (file load + `C4_STATE_DIR`).
 *
 * @param payload - Simulation-shaped object
 */
function writeTempFixture(payload) {
    const absolute = fixtureFile(TEMP_FIXTURE);
    mkdirSync(dirname(absolute), { recursive: true });
    writeFileSync(absolute, JSON.stringify(payload));
}

describe('replay', { concurrency: false }, () => {
    after(() => {
        rmSync(fixtureDir(TEMP_FIXTURE), { recursive: true, force: true });
    });

    describe.skip('replay helpers', () => {
        it('reconstructs txParams from the top-level trace call', () => {
            const tx = txParamsFromSimulation(WETH_DEPOSIT_RESULT);
            assert.equal(tx.to, TX_PARAMS.to);
            assert.equal(tx.from, TX_PARAMS.from);
            assert.equal(tx.data, TX_PARAMS.data);
            assert.equal(tx.value, TX_PARAMS.value);
        });

        it('rejects simulations without a top-level to address', () => {
            assert.throws(() => txParamsFromSimulation({}), /no trace\[0\]\.to/);
            assert.throws(() => txParamsFromSimulation({ trace: [] }), /no trace\[0\]\.to/);
            assert.throws(() => txParamsFromSimulation({ trace: [{ from: '0x1' }] }), /no trace\[0\]\.to/);
            assert.throws(() => txParamsFromSimulation(null), /no trace\[0\]\.to/);
        });

        it('maps a fixture file to test/data/{testname} for C4_STATE_DIR', () => {
            const dir = fixtureDir('state1/0xabc_sim.json');
            assert.match(dir, /test\/data\/state1$/);
            assert.match(fixtureFile('state1/0xabc_sim.json'), /test\/data\/state1\/0xabc_sim\.json$/);
            assert.throws(() => fixtureDir('orphan.json'), /testname\/file/);
            assert.throws(() => fixtureDir('./file.json'), /testname\/file/);
            assert.throws(() => fixtureDir('../file.json'), /testname\/file/);
            assert.throws(() => fixtureDir('/abs.json'), /testname\/file/);
            assert.throws(() => fixtureDir('state1/\0x.json'), /testname\/file/);
            assert.throws(() => fixtureDir('state1/../cache.test.mjs'), /escaped/);
            assert.throws(() => fixtureFile('../package.json'), /escaped test\/data/);
        });

        it('counts C-core event names and only named storage slots', () => {
            const context = {
                decodedEvents: [null, { name: 'Deposit' }],
                decodedTrace: [{ name: 'deposit' }],
                decodedCall: { name: 'deposit' },
                resolvedStorage: new Map([
                    ['0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2', [
                        { variableName: 'balanceOf', baseSlot: 3 },
                    ]],
                ]),
            };
            const counts = countDecoded(WETH_DEPOSIT_RESULT, context);
            assert.equal(counts.events, 2);
            assert.deepEqual(counts.eventNames, ['Transfer', 'Deposit']);
            assert.equal(counts.calls, 1);
            assert.equal(counts.stateChanges, 1);
            assert.equal(counts.unnamedStorage.length, 0);
        });

        it('prefers enrich-decoded event names over C-core names', () => {
            const result = {
                logs: [
                    { name: 'Transfer' },
                    { name: 'Approval' },
                    {},
                    {},
                ],
            };
            const context = {
                decodedEvents: [
                    { name: 'TransferFrom' },
                    null,
                    { name: 'Deposit' },
                    { name: '' },
                ],
            };
            const counts = countDecoded(result, context);
            assert.deepEqual(counts.eventNames, ['TransferFrom', 'Approval', 'Deposit']);
            assert.equal(counts.events, 3);
        });

        it('counts zero events when logs are missing or unnamed', () => {
            assert.equal(countDecoded({}, {}).events, 0);
            assert.equal(countDecoded({ logs: [{ raw: {} }] }, { decodedEvents: [] }).events, 0);
        });

        it('falls back to decodedCall only when the trace is empty', () => {
            const decodedCall = { name: 'transferFrom' };
            const emptyTrace = countDecoded(
                { logs: [], trace: [] },
                { decodedCall, decodedTrace: [{ name: 'ignored' }] },
            );
            assert.deepEqual(emptyTrace.callNames, ['transferFrom']);

            const missingTrace = countDecoded({ logs: [] }, { decodedCall });
            assert.deepEqual(missingTrace.callNames, ['transferFrom']);

            const noName = countDecoded({ logs: [], trace: [] }, { decodedCall: {} });
            assert.equal(noName.calls, 0);

            const nonEmptyTrace = countDecoded(
                { logs: [], trace: [{ to: '0x1', input: '0x' }] },
                { decodedCall, decodedTrace: [null] },
            );
            assert.equal(nonEmptyTrace.calls, 0);
            assert.deepEqual(nonEmptyTrace.callNames, []);
        });

        it('does not count storage without a variableName', () => {
            const context = {
                decodedEvents: [],
                decodedTrace: [],
                resolvedStorage: new Map([
                    ['0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2', [
                        { baseSlot: 3, keys: [{ type: 'address', value: '0x3610' }] },
                    ]],
                ]),
            };
            const counts = countDecoded(WETH_DEPOSIT_RESULT, context);
            assert.equal(counts.stateChanges, 0);
            assert.equal(counts.unnamedStorage.length, 1);
            assert.equal(counts.unnamedStorage[0].baseSlot, 3);
        });

        it('counts mixed named and unnamed storage slots', () => {
            const result = {
                logs: [],
                stateChanges: [{
                    address: '0xAbC',
                    storage: [
                        { slot: '0x01' },
                        { slot: '0x02' },
                        { slot: '0x03' },
                    ],
                }],
            };
            const context = {
                resolvedStorage: new Map([
                    ['0xabc', [
                        { variableName: 'balances', baseSlot: 0 },
                        { baseSlot: 1 },
                        { variableName: 'allowances', baseSlot: 2 },
                    ]],
                ]),
            };
            const counts = countDecoded(result, context);
            assert.equal(counts.stateChanges, 2);
            assert.deepEqual(counts.storageNames, ['0xabc:balances', '0xabc:allowances']);
            assert.equal(counts.unnamedStorage.length, 1);
            assert.equal(counts.unnamedStorage[0].slot, '0x02');
            assert.equal(counts.unnamedStorage[0].baseSlot, 1);
        });

        it('treats empty variableName and missing slot metadata as unnamed', () => {
            const result = {
                stateChanges: [{
                    address: '0xabc',
                    storage: [{ slot: '0xaa' }, { slot: '0xbb' }],
                }],
            };
            const context = {
                resolvedStorage: new Map([
                    ['0xabc', [{ variableName: '' }]],
                ]),
            };
            const counts = countDecoded(result, context);
            assert.equal(counts.stateChanges, 0);
            assert.equal(counts.unnamedStorage.length, 2);
            assert.equal(counts.unnamedStorage[0].slot, '0xaa');
            assert.equal(counts.unnamedStorage[1].slot, '0xbb');
            assert.equal(counts.unnamedStorage[1].baseSlot, undefined);
        });

        it('rejects invalid minimum counts before touching fixtures', async () => {
            await assert.rejects(
                () => assertEnrichedFixture('state1/missing.json', -1, 0, 0),
                /minEvents must be a non-negative integer/,
            );
            await assert.rejects(
                () => assertEnrichedFixture('state1/missing.json', 0, 1.5, 0),
                /minCalls must be a non-negative integer/,
            );
            await assert.rejects(
                () => assertEnrichedFixture('state1/missing.json', 0, 0, Number.NaN),
                /minStateChanges must be a non-negative integer/,
            );
            await assert.rejects(
                () => assertEnrichedFixture('state1/missing.json', '1', 0, 0),
                /minEvents must be a non-negative integer/,
            );
        });

        it('restores C4_STATE_DIR when enrichSimulation throws', async () => {
            writeTempFixture({
                trace: [{ to: '0x1', from: '0x2', input: '0x', value: '0x0' }],
            });
            const previous = process.env.C4_STATE_DIR;
            process.env.C4_STATE_DIR = '/tmp/c4-replay-sentinel';
            try {
                // Force a throw inside enrichSimulation after C4_STATE_DIR is set.
                await assert.rejects(
                    () => assertEnrichedFixture(TEMP_FIXTURE, 0, 0, 0, {
                        txParams: {
                            to: { toLowerCase() { throw new Error('forced enrich failure'); } },
                            from: '0x2',
                            data: '0x',
                            value: '0x0',
                        },
                    }),
                    /forced enrich failure/,
                );
                assert.equal(process.env.C4_STATE_DIR, '/tmp/c4-replay-sentinel');
            } finally {
                if (previous === undefined) delete process.env.C4_STATE_DIR;
                else process.env.C4_STATE_DIR = previous;
            }
        });

        it('clears C4_STATE_DIR on throw when it was previously unset', async () => {
            writeTempFixture({
                trace: [{ to: '0x1', from: '0x2', input: '0x', value: '0x0' }],
            });
            const previous = process.env.C4_STATE_DIR;
            delete process.env.C4_STATE_DIR;
            try {
                await assert.rejects(
                    () => assertEnrichedFixture(TEMP_FIXTURE, 0, 0, 0, {
                        txParams: {
                            to: { toLowerCase() { throw new Error('forced enrich failure'); } },
                            from: '0x2',
                            data: '0x',
                            value: '0x0',
                        },
                    }),
                    /forced enrich failure/,
                );
                assert.equal(process.env.C4_STATE_DIR, undefined);
            } finally {
                if (previous === undefined) delete process.env.C4_STATE_DIR;
                else process.env.C4_STATE_DIR = previous;
            }
        });
    });

    describe('recorded simulations', () => {
        // PAX (USDP) transferFrom via AdminUpgradeabilityProxy. Events and the
        // transferFrom call should decode; all 3 storage writes (balances[from],
        // balances[to], allowances[owner][spender]) must be named.
        it('decodes PAX transferFrom events, calls and named storage', { timeout: 300_000 }, async () => {
            await assertEnrichedFixture(
                'state1/0x61d1d2b369a1d3dadb1000f7b2bd8305d9e3a669325949be149e5b2284963630_sim.json',
                1,
                1,
                3,
            );
        });

        // Nested proxies: wallet 0x9e0d… DELEGATECALLs 0xba8d…, which STATICCALL/CALL
        // USDC 0xa0b8…, which DELEGATECALLs 0x4350…. Transfer + both USDC balance
        // slots decode. The 4 USDC frames (balanceOf/transfer × CALL+DELEGATECALL)
        // decode; the outer 0xb8dc491b frames need a Sourcify ABI for 0xba8d….
        it('decodes nested USDC proxy events, calls and named storage', { timeout: 300_000 }, async () => {
            await assertEnrichedFixture('state2/sim.json', 1, 4, 2);
        });

        // TransparentUpgradeableProxy 0x0fe9… DELEGATECALLs 0x23db…. Unlimited
        // approve + Approval; the allowance write on the proxy must use the
        // implementation storage layout (`allowances[owner][spender]`).
        it('decodes proxy approve events, calls and named allowance storage', { timeout: 300_000 }, async () => {
            await assertEnrichedFixture(
                'state3/0x0b2f75efd752f219b7758d4479110e9eb54260dc89164eb2b69c24b73b96acba_sim.json',
                1,
                2,
                1,
            );
        });
    });
});
