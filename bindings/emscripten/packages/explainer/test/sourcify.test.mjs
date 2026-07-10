import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { fetchContractMetadata } from '../dist/sourcify.js';

describe('fetchContractMetadata', () => {
    let originalFetch;

    beforeEach(() => {
        originalFetch = globalThis.fetch;
    });

    afterEach(() => {
        globalThis.fetch = originalFetch;
    });

    it('returns parsed metadata for a verified contract', async () => {
        globalThis.fetch = async (url) => {
            assert.ok(url.includes('/v2/contract/1/0xabc'));
            assert.ok(url.includes('fields=abi,sources,storageLayout'));
            return new Response(JSON.stringify({
                abi: [{ name: 'transfer', type: 'function' }],
                sources: { 'Token.sol': { content: 'pragma solidity ^0.8.0;' } },
                storageLayout: {
                    storage: [{ slot: '0', label: 'totalSupply', type: 't_uint256' }],
                    types: { t_uint256: { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' } },
                },
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        const meta = await fetchContractMetadata('0xabc', 1);
        assert.ok(Array.isArray(meta.abi));
        assert.equal(meta.abi.length, 1);
        assert.ok(meta.sources);
        assert.equal(meta.sources['Token.sol'].content, 'pragma solidity ^0.8.0;');
        assert.ok(meta.storageLayout);
        assert.equal(meta.storageLayout.storage[0].label, 'totalSupply');
    });

    it('returns null fields for unverified contracts (404)', async () => {
        globalThis.fetch = async () => new Response('Not Found', { status: 404 });

        const meta = await fetchContractMetadata('0xdead', 1);
        assert.equal(meta.abi, null);
        assert.equal(meta.sources, null);
        assert.equal(meta.storageLayout, null);
    });

    it('returns null fields when storageLayout is null (old compiler)', async () => {
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [{ name: 'deposit', type: 'function' }],
            sources: { 'WETH.sol': { content: 'contract WETH {}' } },
            storageLayout: null,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const meta = await fetchContractMetadata('0xweth', 1);
        assert.ok(meta.abi);
        assert.ok(meta.sources);
        assert.equal(meta.storageLayout, null);
    });

    it('handles network errors gracefully', async () => {
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        const meta = await fetchContractMetadata('0xabc', 1);
        assert.equal(meta.abi, null);
        assert.equal(meta.sources, null);
        assert.equal(meta.storageLayout, null);
    });

    it('uses custom baseUrl', async () => {
        globalThis.fetch = async (url) => {
            assert.ok(url.startsWith('https://my-sourcify.example/v2/'));
            return new Response(JSON.stringify({ abi: [], sources: null, storageLayout: null }), {
                status: 200, headers: { 'Content-Type': 'application/json' },
            });
        };

        const meta = await fetchContractMetadata('0xabc', 1, 'https://my-sourcify.example');
        assert.ok(meta);
    });

    it('rejects empty storageLayout storage arrays', async () => {
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [],
            sources: null,
            storageLayout: { storage: [], types: null },
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const meta = await fetchContractMetadata('0xrouter', 1);
        assert.equal(meta.storageLayout, null);
    });
});
