import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { extractStorageLayout } from '../dist/layout.js';
import {
    setExplainerLogLevel, setExplainerLogSink, resetExplainerLogForTests,
} from '../dist/log.js';

const WETH_SOURCE = `pragma solidity ^0.4.18;
contract WETH9 {
    string public name     = "Wrapped Ether";
    string public symbol   = "WETH";
    uint8  public decimals = 18;

    event  Approval(address indexed src, address indexed guy, uint wad);
    event  Transfer(address indexed src, address indexed dst, uint wad);
    event  Deposit(address indexed dst, uint wad);
    event  Withdrawal(address indexed src, uint wad);

    mapping (address => uint)                       public  balanceOf;
    mapping (address => mapping (address => uint))  public  allowance;

    function() public payable { deposit(); }
    function deposit() public payable {
        balanceOf[msg.sender] += msg.value;
        Deposit(msg.sender, msg.value);
    }
    function withdraw(uint wad) public {
        require(balanceOf[msg.sender] >= wad);
        balanceOf[msg.sender] -= wad;
        msg.sender.transfer(wad);
        Withdrawal(msg.sender, wad);
    }
}`;

describe('extractStorageLayout', () => {
    it('extracts WETH9 layout from 0.4.x source', async () => {
        const layout = await extractStorageLayout(
            { 'WETH9.sol': { content: WETH_SOURCE } },
            'WETH9',
        );

        assert.ok(layout);
        assert.ok(layout.storage.length === 5);

        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['name', 'symbol', 'decimals', 'balanceOf', 'allowance']);

        const balanceOf = layout.storage.find(s => s.label === 'balanceOf');
        assert.equal(balanceOf.slot, '3');

        const allowance = layout.storage.find(s => s.label === 'allowance');
        assert.equal(allowance.slot, '4');

        assert.ok(layout.types);
        assert.ok(layout.types['t_mapping(t_address,t_uint256)']);
    });

    it('handles inheritance and packing', async () => {
        const source = `pragma solidity ^0.8.0;
contract Base {
    uint256 public baseVal;
}
contract Child is Base {
    uint128 public counter;
    uint128 public limit;
    mapping(address => uint) public balances;
}`;

        const layout = await extractStorageLayout(
            { 'test.sol': { content: source } },
            'Child',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.ok(names.includes('baseVal'));
        assert.ok(names.includes('counter'));
        assert.ok(names.includes('limit'));
        assert.ok(names.includes('balances'));

        // baseVal at slot 0 (32 bytes), counter+limit packed at slot 1
        const counter = layout.storage.find(s => s.label === 'counter');
        const limit = layout.storage.find(s => s.label === 'limit');
        assert.equal(counter.slot, '1');
        assert.equal(limit.slot, '1');
        assert.equal(counter.slot, limit.slot);
    });

    it('handles structs and enums', async () => {
        const source = `pragma solidity ^0.8.0;
struct Info { uint256 id; address owner; }
enum State { Active, Paused }
contract Test {
    Info public info;
    State public state;
    Info[] public infos;
}`;

        const layout = await extractStorageLayout(
            { 'test.sol': { content: source } },
            'Test',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.ok(names.includes('info'));
        assert.ok(names.includes('state'));
        assert.ok(names.includes('infos'));
    });

    it('excludes constant and immutable variables', async () => {
        const source = `pragma solidity ^0.8.0;
contract Test {
    uint256 constant MAX = 100;
    uint256 immutable deployed = block.number;
    uint256 public counter;
}`;

        const layout = await extractStorageLayout(
            { 'test.sol': { content: source } },
            'Test',
        );

        assert.ok(layout);
        assert.equal(layout.storage.length, 1);
        assert.equal(layout.storage[0].label, 'counter');
        assert.equal(layout.storage[0].slot, '0');
    });

    it('uses last contract when contractName is omitted', async () => {
        const source = `pragma solidity ^0.8.0;
contract A { uint256 public x; }
contract B { uint256 public y; uint256 public z; }`;

        const layout = await extractStorageLayout({ 'test.sol': { content: source } });

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.ok(names.includes('y'));
        assert.ok(names.includes('z'));
    });

    it('returns null for unparseable source', async () => {
        const layout = await extractStorageLayout(
            { 'bad.sol': { content: 'this is not solidity' } },
            'Test',
        );
        assert.equal(layout, null);
    });

    it('logs a warning when the skeleton fails to compile', async () => {
        const lines = [];
        setExplainerLogSink((level, message, extra) => { lines.push({ level, message, extra }); });
        setExplainerLogLevel('warn');
        try {
            const source = `pragma solidity ^0.8.0;
contract Test {
    UnknownType public x;
}`;
            const layout = await extractStorageLayout(
                { 'test.sol': { content: source } },
                'Test',
            );
            assert.equal(layout, null);
            const hit = lines.find(l => l.level === 'warn' && l.message === 'skeleton compile errors');
            assert.ok(hit, `expected skeleton compile warning, got ${JSON.stringify(lines)}`);
            assert.equal(hit.extra.scope, 'layout');
            assert.equal(hit.extra.contract, 'Test');
            assert.equal(typeof hit.extra.error, 'string');
            assert.ok(hit.extra.error.length > 0);
            assert.ok(hit.extra.count >= 1);
        } finally {
            resetExplainerLogForTests();
        }
    });

    it('includes interfaces used only as state-variable types', async () => {
        const source = `pragma solidity ^0.8.0;
interface IRouter {
    function weth() external view returns (address);
}
contract Token {
    mapping(address => mapping(address => uint256)) private _allowances;
    IRouter private router;
    address public pair;
}`;

        const layout = await extractStorageLayout(
            { 'Token.sol': { content: source } },
            'Token',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['_allowances', 'router', 'pair']);
        assert.equal(layout.storage.find(s => s.label === '_allowances').slot, '0');
    });

    it('includes sibling contracts used only as state-variable types', async () => {
        const source = `pragma solidity ^0.8.0;
contract Pair {
    address public token0;
    address public token1;
}
contract Token {
    mapping(address => uint256) public balances;
    Pair public pair;
}`;

        const layout = await extractStorageLayout(
            { 'Token.sol': { content: source } },
            'Token',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['balances', 'pair']);
        assert.equal(layout.storage.find(s => s.label === 'balances').slot, '0');
        assert.equal(layout.storage.find(s => s.label === 'pair').slot, '1');
    });

    it('does not mix an unrelated sibling contract into the target layout', async () => {
        const source = `pragma solidity ^0.8.0;
contract Extra {
    uint256 public extraVal;
    mapping(address => uint256) public extraMap;
}
contract Token {
    uint256 public tokenVal;
}`;

        const layout = await extractStorageLayout(
            { 'Token.sol': { content: source } },
            'Token',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['tokenVal']);
        assert.equal(layout.storage[0].slot, '0');
    });

    it('handles old-style uint -> uint256 normalization', async () => {
        const source = `pragma solidity ^0.4.18;
contract Test {
    uint public value;
    mapping(uint => uint) public lookup;
}`;

        const layout = await extractStorageLayout(
            { 'test.sol': { content: source } },
            'Test',
        );

        assert.ok(layout);
        assert.equal(layout.storage.length, 2);

        const valueEntry = layout.storage.find(s => s.label === 'value');
        assert.ok(valueEntry.type.includes('uint256'));
    });

    it('maps omitted parser visibility default to internal for storage gaps', async () => {
        const source = `pragma solidity ^0.5.0;
contract Base {
    uint256[24] __gap;
}
contract Test is Base {
    uint256 public x;
}`;

        const layout = await extractStorageLayout(
            { 'test.sol': { content: source } },
            'Test',
        );

        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['__gap', 'x']);
        assert.equal(layout.storage.find(s => s.label === '__gap').slot, '0');
        assert.equal(layout.storage.find(s => s.label === 'x').slot, '24');
    });

    it('deduplicates identically named enums and structs from multiple files', async () => {
        const rounding = `pragma solidity ^0.8.0;
enum Rounding { Down, Up, Zero }
struct Info { uint256 id; address owner; }`;
        const sources = {
            'Math.sol': {
                content: `${rounding}
contract Math {}` },
            'MathUpgradeable.sol': {
                content: `${rounding}
contract Token {
    Info public info;
    Rounding public mode;
}` },
        };

        const layout = await extractStorageLayout(sources, 'Token');
        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['info', 'mode']);
    });

    it('flattens library-qualified struct types to file-scope names', async () => {
        const source = `pragma solidity ^0.8.0;
library RateLimit {
    struct LimitConfig { uint256 limitCapacity; uint256 refillPerSecond; }
    struct Storage {
        LimitConfig limitConfig;
        uint256 remainingAmount;
        uint256 lastRefillTime;
    }
}
library EnumerableSet {
    struct Set { bytes32[] _values; mapping(bytes32 => uint256) _indexes; }
    struct AddressSet { Set _inner; }
}
struct SupplyController {
    RateLimit.Storage rateLimitStorage;
    EnumerableSet.AddressSet mintAddressWhitelist;
    bool allowAnyMintAndBurnAddress;
}
contract Token {
    mapping(address => SupplyController) internal supplyControllerMap;
    EnumerableSet.AddressSet internal supplyControllerSet;
    uint256 public x;
}`;

        const layout = await extractStorageLayout({ 'Token.sol': { content: source } }, 'Token');
        assert.ok(layout);
        const names = layout.storage.map(s => s.label);
        assert.deepEqual(names, ['supplyControllerMap', 'supplyControllerSet', 'x']);
        assert.equal(layout.storage.find(s => s.label === 'supplyControllerMap').slot, '0');
        assert.equal(layout.storage.find(s => s.label === 'supplyControllerSet').slot, '1');
        assert.equal(layout.storage.find(s => s.label === 'x').slot, '3');
    });

    it('emits a referenced struct before the struct that uses it', async () => {
        const source = `pragma solidity ^0.8.0;
struct Outer {
    Inner inner;
    uint256 extra;
}
struct Inner {
    uint256 value;
}
contract Token {
    Outer public data;
}`;

        const layout = await extractStorageLayout({ 'Token.sol': { content: source } }, 'Token');
        assert.ok(layout);
        assert.equal(layout.storage[0].label, 'data');
        assert.equal(layout.storage[0].slot, '0');
    });
});
