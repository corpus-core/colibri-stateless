import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { getBundledCompiler, compileAndVerify } from '../dist/compiler.js';
import { keccak256 } from 'ethers';

describe('getBundledCompiler', () => {
    it('returns a compiler with compile and version methods', async () => {
        const compiler = await getBundledCompiler();
        assert.ok(typeof compiler.compile === 'function');
        assert.ok(typeof compiler.version === 'function');
        assert.ok(compiler.version().startsWith('0.8.'));
    });

    it('compiles a simple contract', async () => {
        const compiler = await getBundledCompiler();
        const input = JSON.stringify({
            language: 'Solidity',
            sources: { 'test.sol': { content: 'pragma solidity >=0.8.0; contract T { uint256 public x; }' } },
            settings: { outputSelection: { '*': { '*': ['abi', 'evm.deployedBytecode.object'] } } },
        });

        const output = JSON.parse(compiler.compile(input));
        assert.ok(output.contracts);
        assert.ok(output.contracts['test.sol']['T'].abi);
        assert.ok(output.contracts['test.sol']['T'].evm.deployedBytecode.object);
    });
});

describe('compileAndVerify', () => {
    it('verifies matching bytecode against codeHash', async () => {
        const compiler = await getBundledCompiler();
        const source = 'pragma solidity >=0.8.0; contract T { uint256 public x; }';

        const input = JSON.stringify({
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['evm.deployedBytecode.object'] } } },
        });

        const output = JSON.parse(compiler.compile(input));
        const bytecode = output.contracts['test.sol']['T'].evm.deployedBytecode.object;
        const expectedHash = keccak256('0x' + bytecode);

        const stdJsonInput = {
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['abi'] } } },
        };

        const result = await compileAndVerify(
            stdJsonInput,
            compiler.version(),
            expectedHash,
            { 'test.sol': { content: source } },
        );

        assert.equal(result.verified, true);
        assert.ok(Array.isArray(result.abi));
        assert.ok(result.sources);
    });

    it('returns verified=false for mismatched codeHash', async () => {
        const compiler = await getBundledCompiler();
        const source = 'pragma solidity >=0.8.0; contract T { uint256 public x; }';

        const stdJsonInput = {
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['abi'] } } },
        };

        const result = await compileAndVerify(
            stdJsonInput,
            compiler.version(),
            '0x0000000000000000000000000000000000000000000000000000000000000000',
            { 'test.sol': { content: source } },
        );

        assert.equal(result.verified, false);
    });

    it('returns verified=false for empty sources', async () => {
        const compiler = await getBundledCompiler();
        const result = await compileAndVerify(
            { language: 'Solidity', sources: {}, settings: {} },
            compiler.version(),
            '0x1234',
            {},
        );

        assert.equal(result.verified, false);
    });
});
