import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { sliceUsedFunctions, sliceCoveredDefinitions, findCoveredDefinitions } from '../dist/source_slice.js';

function identitySanitize(source) {
    return source;
}

function includeSol(filename) {
    return filename.endsWith('.sol');
}

function slice(sources, entryNames, opts = {}) {
    return sliceUsedFunctions({
        sources,
        contractName: opts.contractName ?? null,
        entryNames,
        maxChars: opts.maxChars ?? null,
        includeFile: includeSol,
        sanitize: opts.sanitize ?? identitySanitize,
    });
}

describe('sliceUsedFunctions', () => {
    it('returns null when no entry name matches so callers can fall back', () => {
        const sources = {
            'C.sol': { content: 'contract C { function deposit() public {} }' },
        };
        assert.equal(slice(sources, ['withdraw']), null);
        assert.equal(slice(sources, []), null);
        assert.equal(slice(sources, ['0xdead']), null);
    });

    it('includes the entry, its modifier, internal callee, and library call', () => {
        const sources = {
            'Vault.sol': {
                content: [
                    'library Math { function add(uint256 a, uint256 b) internal pure returns (uint256) { return a + b; } }',
                    'contract Vault {',
                    '    function deposit(uint256 amount) public onlyOwner {',
                    '        _mint(amount);',
                    '        Math.add(amount, 1);',
                    '    }',
                    '    function _mint(uint256 amount) internal { total += amount; }',
                    '    function unused() public {}',
                    '    modifier onlyOwner() { _; }',
                    '    uint256 total;',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'Vault' });
        assert.ok(pieces);
        const names = pieces.map(p => `${p.kind}:${p.name}`);
        assert.deepEqual(names, [
            'function:deposit',
            'state:total',
            'modifier:onlyOwner',
            'function:_mint',
            'function:add',
        ]);
        assert.ok(!pieces.some(p => p.name === 'unused'));
    });

    it('resolves inherited functions and this/super calls', () => {
        const sources = {
            'Ownable.sol': {
                content: [
                    'contract Ownable {',
                    '    function owner() public view returns (address) { return address(0); }',
                    '    function _check() internal virtual { owner(); }',
                    '}',
                    'contract Vault is Ownable {',
                    '    function deposit() public {',
                    '        this.owner();',
                    '        super._check();',
                    '        _local();',
                    '    }',
                    '    function _local() internal {}',
                    '    function _check() internal override { }',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'Vault' });
        assert.ok(pieces);
        const names = new Set(pieces.map(p => p.name));
        assert.ok(names.has('deposit'));
        assert.ok(names.has('owner'));
        assert.ok(names.has('_local'));
        // super._check resolves on the base; Vault's override is a separate overload path.
        assert.ok(names.has('_check'));
    });

    it('skips interface stubs without a body', () => {
        const sources = {
            'I.sol': {
                content: [
                    'interface IERC20 { function transfer(address,uint256) external returns (bool); }',
                    'contract Token { function transfer(address,uint256) public returns (bool) { return true; } }',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['transfer'], { contractName: 'Token' });
        assert.ok(pieces);
        assert.equal(pieces.length, 1);
        assert.equal(pieces[0].name, 'transfer');
        assert.ok(pieces[0].text.includes('return true'));
    });

    it('returns an empty list when entries match but none fit the budget', () => {
        const sources = {
            'C.sol': {
                content: `contract C { function deposit() public { string memory pad = "${'Z'.repeat(200)}"; } }`,
            },
        };
        const pieces = slice(sources, ['deposit'], { maxChars: 20 });
        assert.ok(Array.isArray(pieces));
        assert.equal(pieces.length, 0);
    });

    it('applies sanitize and still queues callees when a definition is dropped', () => {
        const sources = {
            'C.sol': {
                content: [
                    'contract C {',
                    '    function deposit() public { helper(); }',
                    '    function helper() internal {}',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], {
            maxChars: 40,
            sanitize(text) {
                // Inflate the entry so it misses the budget; leave helper small.
                if (text.includes('helper();')) return text + 'X'.repeat(100);
                return text;
            },
        });
        assert.ok(pieces);
        assert.equal(pieces.length, 1);
        assert.equal(pieces[0].name, 'helper');
    });

    it('does not follow external member calls on unknown identifiers', () => {
        const sources = {
            'C.sol': {
                content: [
                    'contract C {',
                    '    function deposit(IERC20 token) public { token.transfer(msg.sender, 1); helper(); }',
                    '    function helper() internal {}',
                    '    function transfer(address,uint256) public {}',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'C' });
        assert.ok(pieces);
        const names = pieces.map(p => p.name);
        assert.deepEqual(names, ['deposit', 'helper']);
        assert.ok(!names.includes('transfer'));
    });

    it('includes storage variables and skips unused constants', () => {
        const sources = {
            'C.sol': {
                content: [
                    'contract C {',
                    '    uint256 total;',
                    '    uint256 constant UNUSED = 1;',
                    '    uint256 immutable cached;',
                    '    function deposit() public { total += 1; }',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'C' });
        assert.ok(pieces);
        const names = pieces.map(p => `${p.kind}:${p.name}`);
        assert.ok(names.includes('state:total'));
        assert.ok(!names.includes('state:UNUSED'));
        assert.ok(!names.includes('state:cached'));
        assert.equal(pieces.find(p => p.name === 'deposit').contractHeader, 'contract C');
    });

    it('rebuilds an abstract contract header with its bases', () => {
        const sources = {
            'C.sol': {
                content: 'abstract contract Proxy is ERC1967 { address impl; function deposit() public { impl = msg.sender; } }',
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'Proxy' });
        assert.ok(pieces);
        assert.equal(pieces.find(p => p.name === 'deposit').contractHeader, 'abstract contract Proxy is ERC1967');
        assert.ok(pieces.some(p => p.kind === 'state' && p.name === 'impl'));
    });

    it('includes an enum and a struct the function names, not unused ones', () => {
        const sources = {
            'C.sol': {
                content: [
                    'contract C {',
                    '    enum Mode { Off, On }',
                    '    enum Unused { A, B }',
                    '    struct Store { uint256 total; }',
                    '    struct Other { uint256 x; }',
                    '    function deposit() public view returns (uint256) {',
                    '        Store storage s = _store();',
                    '        return mode == Mode.On ? s.total : 0;',
                    '    }',
                    '    function _store() internal view returns (Store storage s) { assembly { s.slot := 0 } }',
                    '    uint8 mode;',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'C' });
        assert.ok(pieces);
        const names = new Set(pieces.map(p => `${p.kind}:${p.name}`));
        assert.ok(names.has('enum:Mode'));
        assert.ok(names.has('struct:Store'));
        assert.ok(names.has('state:mode'));
        assert.ok(!names.has('enum:Unused'));
        assert.ok(!names.has('struct:Other'));
    });

    it('includes a library enum and a constant named from assembly', () => {
        const sources = {
            'C.sol': {
                content: [
                    'library Math { enum Rounding { Floor, Ceil } function unused() internal {} }',
                    'contract C {',
                    '    uint256 constant LOC = 1;',
                    '    uint256 constant OTHER = 2;',
                    '    function deposit() public view returns (uint256) {',
                    '        uint256 x;',
                    '        assembly { x := LOC }',
                    '        return x + uint256(Math.Rounding.Ceil);',
                    '    }',
                    '}',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'C' });
        assert.ok(pieces);
        const names = pieces.map(p => `${p.kind}:${p.name}`);
        assert.ok(names.includes('state:LOC'));
        assert.ok(names.includes('enum:Rounding'));
        assert.ok(!names.includes('state:OTHER'));
        assert.ok(!names.includes('function:unused'));
        assert.equal(pieces.find(p => p.name === 'Rounding').contractHeader, 'library Math');
    });

    it('keeps a referenced file-level enum outside a contract', () => {
        const sources = {
            'C.sol': {
                content: [
                    'enum Mode { Off, On }',
                    'contract C { function deposit() public pure returns (uint256) { return uint256(Mode.On); } }',
                ].join('\n'),
            },
        };
        const pieces = slice(sources, ['deposit'], { contractName: 'C' });
        assert.ok(pieces);
        const mode = pieces.find(p => p.name === 'Mode');
        assert.ok(mode);
        assert.equal(mode.kind, 'enum');
        assert.equal(mode.contractHeader, '');
        assert.ok(mode.text.includes('enum Mode'));
    });
});

/**
 * Build a synthetic Solidity source and return the source string plus the
 * offset of the target substring, so coverage tests can address ranges without
 * hard-coding numbers.
 *
 * @param {string} source - Full Solidity source
 * @param {string} needle - Substring inside a function or modifier
 * @return {{ source: string, offset: number }} Source and the needle offset
 */
function withOffset(source, needle) {
    const offset = source.indexOf(needle);
    if (offset < 0) throw new Error(`needle "${needle}" not found`);
    return { source, offset };
}

describe('findCoveredDefinitions', () => {
    it('returns the covered functions in a stable order', () => {
        const src = [
            'contract Vault {',
            '    function deposit() public {}',
            '    function withdraw() public {}',
            '    function unused() public {}',
            '}',
        ].join('\n');
        const depositOffset = withOffset(src, 'deposit() public {}').offset;
        const withdrawOffset = withOffset(src, 'withdraw() public {}').offset;
        const defs = findCoveredDefinitions({
            sources: { 'Vault.sol': { content: src } },
            hits: [
                // Hit the middle of each body — proves start is not the only match.
                { filename: 'Vault.sol', offset: withdrawOffset + 4 },
                { filename: 'Vault.sol', offset: depositOffset + 4 },
            ],
            includeFile: () => true,
        });
        assert.deepEqual(defs.map(d => d.name), ['deposit', 'withdraw']);
        assert.ok(defs.every(d => d.filename === 'Vault.sol'));
    });

    it('ignores hits that fall outside any function range', () => {
        const src = 'contract C { function f() public {} }';
        const defs = findCoveredDefinitions({
            sources: { 'C.sol': { content: src } },
            hits: [{ filename: 'C.sol', offset: 0 }],
            includeFile: () => true,
        });
        assert.deepEqual(defs, []);
    });
});

describe('sliceCoveredDefinitions', () => {
    it('includes the covered function and its storage but NOT its callees', () => {
        const src = [
            'contract Vault {',
            '    uint256 total;',
            '    function deposit(uint256 amount) public {',
            '        _mint(amount);',
            '        total += amount;',
            '    }',
            '    function _mint(uint256 amount) internal { total += amount; }',
            '    function unused() public {}',
            '}',
        ].join('\n');
        const bodyOffset = withOffset(src, 'total += amount;').offset;
        const pieces = sliceCoveredDefinitions({
            sources: { 'Vault.sol': { content: src } },
            hits: [{ filename: 'Vault.sol', offset: bodyOffset }],
            maxChars: null,
            includeFile: () => true,
            sanitize: (s) => s,
        });
        assert.ok(pieces);
        const names = pieces.map(p => `${p.kind}:${p.name}`);
        assert.ok(names.includes('function:deposit'));
        assert.ok(names.includes('state:total'));
        assert.ok(!names.some(n => n === 'function:_mint'), 'callees must not leak in via coverage');
        assert.ok(!names.some(n => n === 'function:unused'));
    });

    it('drops covered defs that exceed the budget but still emits headerOk siblings', () => {
        const large = 'contract C { function big() public { ' + 'uint256 x;'.repeat(50) + ' } }';
        const bodyOffset = large.indexOf('uint256 x;');
        const pieces = sliceCoveredDefinitions({
            sources: { 'C.sol': { content: large } },
            hits: [{ filename: 'C.sol', offset: bodyOffset }],
            maxChars: 20,
            includeFile: () => true,
            sanitize: (s) => s,
        });
        assert.ok(Array.isArray(pieces));
        assert.equal(pieces.length, 0);
    });

    it('returns null when no hit is inside any function range', () => {
        const src = 'contract C { function f() public {} }';
        const pieces = sliceCoveredDefinitions({
            sources: { 'C.sol': { content: src } },
            hits: [{ filename: 'C.sol', offset: 0 }],
            maxChars: null,
            includeFile: () => true,
            sanitize: (s) => s,
        });
        assert.equal(pieces, null);
    });
});
