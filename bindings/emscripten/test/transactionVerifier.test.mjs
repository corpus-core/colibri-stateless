import test from 'node:test';
import assert from 'node:assert/strict';
import { modulePath } from './test_config.js';

const { compareTransactionParameters, TransactionVerifier } = await import(modulePath);

const cfg = { chainId: 1, rpcs: ['http://localhost:8545'] };

const to = '0x3535353535353535353535353535353535353535';
const from = '0x1111111111111111111111111111111111111111';

function decoded(overrides = {}) {
    return {
        type: '0x2',
        nonce: '0x1',
        to,
        from,
        value: '0x0',
        input: '0x',
        gas: '0x5208',
        chainId: '0x1',
        maxFeePerGas: '0x3b9aca00',
        maxPriorityFeePerGas: '0x3b9aca00',
        gasPrice: '0x3b9aca00',
        accessList: [],
        ...overrides
    };
}

test('matching EIP-1559 transfer with data/input alias', () => {
    const original = {
        to,
        value: '0x0',
        data: '0x',
        maxFeePerGas: '0x3b9aca00',
        maxPriorityFeePerGas: '0x3b9aca00'
    };
    assert.equal(compareTransactionParameters(original, decoded(), cfg), true);
});

test('checksum to and quantity aliases match', () => {
    const original = {
        to: '0x3535353535353535353535353535353535353535',
        value: 0,
        gasLimit: 21000,
        chainId: '0x1'
    };
    assert.equal(compareTransactionParameters(original, decoded({ type: '0x0', gas: '0x5208', v: '0x25' }), cfg), true);
});

test('original.input is compared against decoded.input', () => {
    const data = '0xa9059cbb00000000000000000000000011111111111111111111111111111111111111110000000000000000000000000000000000000000000000000000000000000001';
    const original = { to, input: data };
    assert.equal(compareTransactionParameters(original, decoded({ input: data }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ input: data.replace(/01$/, '02') }), cfg), false);
});

test('omitted value defaults to 0 — signer cannot attach ETH', () => {
    const original = { to };
    assert.equal(compareTransactionParameters(original, decoded({ value: '0x0' }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ value: '0xde0b6b3a7640000' }), cfg), false);
});

test('omitted data defaults to empty — 0x00 is not empty', () => {
    const original = { to, value: '0x0' };
    assert.equal(compareTransactionParameters(original, decoded({ input: '0x' }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ input: '0x00' }), cfg), false);
    assert.equal(compareTransactionParameters({ to, data: null }, decoded({ input: '0xdead' }), cfg), false);
});

test('omitted to is contract creation — signer cannot set a recipient', () => {
    const original = { data: '0x6001600055' };
    assert.equal(compareTransactionParameters(original, decoded({ to: null, input: '0x6001600055' }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ to, input: '0x6001600055' }), cfg), false);
});

test('changed to is rejected', () => {
    const original = { to, value: '0x1' };
    assert.equal(
        compareTransactionParameters(
            original,
            decoded({ to: '0x2222222222222222222222222222222222222222', value: '0x1' }),
            cfg
        ),
        false
    );
});

test('from is enforced when the dapp set it', () => {
    const original = { to, from };
    assert.equal(compareTransactionParameters(original, decoded({ from }), cfg), true);
    assert.equal(
        compareTransactionParameters(original, decoded({ from: '0x2222222222222222222222222222222222222222' }), cfg),
        false
    );
});

test('wallet may pick nonce and gas when omitted, but not when set', () => {
    const original = { to };
    assert.equal(compareTransactionParameters(original, decoded({ nonce: '0x99', gas: '0x186a0' }), cfg), true);
    assert.equal(compareTransactionParameters({ to, nonce: '0x1' }, decoded({ nonce: '0x2' }), cfg), false);
    assert.equal(compareTransactionParameters({ to, gas: '0x5208' }, decoded({ gas: '0x5209' }), cfg), false);
});

test('pre-EIP-155 (missing chainId) is rejected', () => {
    const original = { to };
    const { chainId, ...noChain } = decoded();
    assert.equal(compareTransactionParameters(original, noChain, cfg), false);
});

test('chainId must match config', () => {
    const original = { to };
    assert.equal(compareTransactionParameters(original, decoded({ chainId: '0xaa36a7' }), cfg), false);
    assert.equal(compareTransactionParameters({ to, chainId: 11155111 }, decoded(), cfg), false);
});

test('authorizationList defaults to empty — type-4 upgrade is rejected', () => {
    const original = { to, value: '0x0', data: '0x' };
    const auth = [{
        address: '0x3333333333333333333333333333333333333333',
        chainId: '0x1',
        nonce: '0x0',
        yParity: '0x0',
        r: '0x' + '11'.repeat(32),
        s: '0x' + '22'.repeat(32)
    }];
    assert.equal(compareTransactionParameters(original, decoded({ authorizationList: auth }), cfg), false);
    assert.equal(compareTransactionParameters({ ...original, authorizationList: auth }, decoded({ authorizationList: auth }), cfg), true);
    assert.equal(
        compareTransactionParameters(
            { ...original, authorizationList: auth },
            decoded({
                authorizationList: [{ ...auth[0], address: '0x4444444444444444444444444444444444444444' }]
            }),
            cfg
        ),
        false
    );
});

test('matching blob hashes are accepted without original maxFeePerBlobGas', () => {
    const hash = '0x01' + '00'.repeat(31);
    const original = { to, blobVersionedHashes: [hash] };
    assert.equal(
        compareTransactionParameters(
            original,
            decoded({ blobVersionedHashes: [hash], maxFeePerBlobGas: '0x1' }),
            cfg
        ),
        true
    );
});

test('accessList and blob hashes cannot be added by the signer', () => {
    const original = { to };
    assert.equal(
        compareTransactionParameters(
            original,
            decoded({ accessList: [{ address: to, storageKeys: ['0x' + '00'.repeat(32)] }] }),
            cfg
        ),
        false
    );
    assert.equal(
        compareTransactionParameters(
            original,
            decoded({ blobVersionedHashes: ['0x01' + '00'.repeat(31)] }),
            cfg
        ),
        false
    );
});

test('odd-length calldata is invalid, not padded to a different byte string', () => {
    const original = { to, data: '0x1' };
    assert.equal(compareTransactionParameters(original, decoded({ input: '0x01' }), cfg), false);
});

test('conflicting data and input aliases are rejected', () => {
    const original = { to, data: '0x01', input: '0x02' };
    assert.equal(compareTransactionParameters(original, decoded({ input: '0x01' }), cfg), false);
});

test('EIP-1559 effective gasPrice is not compared against original.gasPrice', () => {
    const original = {
        to,
        maxFeePerGas: '0x3b9aca00',
        maxPriorityFeePerGas: '0x3b9aca00',
        gasPrice: '0x1'
    };
    assert.equal(compareTransactionParameters(original, decoded({ gasPrice: '0x3b9aca00' }), cfg), true);
});

test('legacy gasPrice is compared when the dapp set it', () => {
    const original = { to, gasPrice: '0x3b9aca00' };
    const legacy = { type: '0x0', gasPrice: '0x3b9aca00', v: '0x25' };
    assert.equal(compareTransactionParameters(original, decoded(legacy), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ ...legacy, gasPrice: '0x1' }), cfg), false);
});

test('pre-EIP-155 legacy v=27/28 is rejected even with chainId 1', () => {
    const original = { to, value: '0x1' };
    const unprotected = decoded({ type: '0x0', value: '0x1', chainId: '0x1', v: '0x1b' });
    assert.equal(compareTransactionParameters(original, unprotected, cfg), false);
    assert.equal(compareTransactionParameters(original, decoded({ type: '0x0', value: '0x1', v: '0x25' }), cfg), true);
});

test('verifyAndSendTransaction broadcasts only after a matching decode', async () => {
    const signed = '0x02dead';
    const original = { to, value: '0x0' };
    const calls = [];
    const hash = await TransactionVerifier.verifyAndSendTransaction(
        original,
        {
            ...cfg,
            fallback_provider: {
                request: async (req) => {
                    calls.push(req.method);
                    return signed;
                }
            }
        },
        async (method, args) => {
            calls.push(method);
            assert.equal(method, 'colibri_decodeTransaction');
            assert.deepEqual(args, [signed]);
            return decoded({ value: '0x0' });
        },
        async (_urls, payload) => {
            calls.push(payload.method);
            assert.deepEqual(payload.params, [signed]);
            return '0xabc';
        }
    );
    assert.equal(hash, '0xabc');
    assert.deepEqual(calls, ['eth_signTransaction', 'colibri_decodeTransaction', 'eth_sendRawTransaction']);
});

test('verifyAndSendTransaction does not broadcast a mutated value', async () => {
    await assert.rejects(
        () => TransactionVerifier.verifyAndSendTransaction(
            { to },
            {
                ...cfg,
                fallback_provider: { request: async () => '0x02dead' }
            },
            async () => decoded({ value: '0xde0b6b3a7640000' }),
            async () => {
                throw new Error('must not broadcast');
            }
        ),
        (err) => err.code === 4202 && /does not match original parameters/.test(err.message)
    );
});

test('original.data is compared against decoded.input', () => {
    const data = '0xa9059cbb00000000000000000000000011111111111111111111111111111111111111110000000000000000000000000000000000000000000000000000000000000001';
    const original = { to, data };
    assert.equal(compareTransactionParameters(original, decoded({ input: data }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ input: data.replace(/01$/, '02') }), cfg), false);
});

test('decoded.data is accepted when input is absent', () => {
    const data = '0xabcdef';
    const { input: _input, ...rest } = decoded();
    assert.equal(compareTransactionParameters({ to, data }, { ...rest, data }, cfg), true);
});

test('gas and gasLimit aliases must agree', () => {
    assert.equal(compareTransactionParameters({ to, gas: '0x5208', gasLimit: 21000 }, decoded({ gas: '0x5208' }), cfg), true);
    assert.equal(compareTransactionParameters({ to, gas: '0x5208', gasLimit: '0x5209' }, decoded({ gas: '0x5208' }), cfg), false);
    assert.equal(compareTransactionParameters({ to, gasLimit: '0x5208' }, decoded({ gas: '0x5209' }), cfg), false);
});

test('from is not compared when omitted', () => {
    assert.equal(
        compareTransactionParameters({ to }, decoded({ from: '0x2222222222222222222222222222222222222222' }), cfg),
        true
    );
});

test('checksum to matches decoded lowercase', () => {
    const checksum = '0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed';
    assert.equal(
        compareTransactionParameters({ to: checksum }, decoded({ to: checksum.toLowerCase() }), cfg),
        true
    );
});

test('empty to is treated as contract creation', () => {
    const init = '0x6001600055';
    assert.equal(compareTransactionParameters({ to: '0x', data: init }, decoded({ to: null, input: init }), cfg), true);
    assert.equal(compareTransactionParameters({ to: '0x', data: init }, decoded({ to, input: init }), cfg), false);
});

test('matching accessList is accepted; mutated storageKeys are not', () => {
    const key0 = '0x' + '00'.repeat(32);
    const key1 = '0x' + '01'.repeat(32);
    const original = { to, accessList: [{ address: to, storageKeys: [key0] }] };
    assert.equal(compareTransactionParameters(original, decoded({ accessList: [{ address: to, storageKeys: [key0] }] }), cfg), true);
    assert.equal(compareTransactionParameters(original, decoded({ accessList: [{ address: to, storageKeys: [key1] }] }), cfg), false);
});

test('blobVersionedHashes value mismatch is rejected', () => {
    const hash = '0x01' + '00'.repeat(31);
    assert.equal(
        compareTransactionParameters(
            { to, blobVersionedHashes: [hash] },
            decoded({ blobVersionedHashes: ['0x02' + '00'.repeat(31)] }),
            cfg
        ),
        false
    );
});

test('wallet may pick fees when omitted, but not when set', () => {
    assert.equal(compareTransactionParameters({ to }, decoded({ maxFeePerGas: '0x1', maxPriorityFeePerGas: '0x1' }), cfg), true);
    const original = { to, maxFeePerGas: '0x3b9aca00', maxPriorityFeePerGas: '0x3b9aca00' };
    assert.equal(compareTransactionParameters(original, decoded({ maxFeePerGas: '0x1' }), cfg), false);
    assert.equal(compareTransactionParameters(original, decoded({ maxPriorityFeePerGas: '0x1' }), cfg), false);
});

test('type is compared when the dapp set it', () => {
    assert.equal(compareTransactionParameters({ to, type: '0x2' }, decoded({ type: '0x2' }), cfg), true);
    assert.equal(compareTransactionParameters({ to, type: '0x2' }, decoded({ type: '0x0' }), cfg), false);
});

test('signer cannot inject mint, sourceHash, or isSystemTx', () => {
    assert.equal(compareTransactionParameters({ to }, decoded({ mint: '0x1' }), cfg), false);
    assert.equal(compareTransactionParameters({ to }, decoded({ sourceHash: '0x' + '11'.repeat(32) }), cfg), false);
    assert.equal(compareTransactionParameters({ to }, decoded({ isSystemTx: true }), cfg), false);
});
