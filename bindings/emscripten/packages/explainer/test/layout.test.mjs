import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { extractStorageLayout } from '../dist/layout.js';

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
});
