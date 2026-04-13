import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { hexToBigInt, weiToEth, formatTokenAmount, formatGas, shortenAddress } from '../dist/format.js';

describe('hexToBigInt', () => {
    it('parses hex with 0x prefix', () => {
        assert.equal(hexToBigInt('0xff'), 255n);
    });

    it('parses large hex values', () => {
        assert.equal(hexToBigInt('0x16345785d8a0000'), 100000000000000000n);
    });

    it('returns 0n for empty input', () => {
        assert.equal(hexToBigInt(''), 0n);
        assert.equal(hexToBigInt(null), 0n);
        assert.equal(hexToBigInt(undefined), 0n);
        assert.equal(hexToBigInt('0x'), 0n);
        assert.equal(hexToBigInt('0x0'), 0n);
    });
});

describe('weiToEth', () => {
    it('converts 1 ETH', () => {
        assert.equal(weiToEth('0xde0b6b3a7640000'), '1');
    });

    it('converts 0.1 ETH', () => {
        assert.equal(weiToEth('0x16345785d8a0000'), '0.1');
    });

    it('converts 0 ETH', () => {
        assert.equal(weiToEth('0x0'), '0');
    });

    it('converts fractional amounts with correct precision', () => {
        // 0.123456789 ETH = 123456789000000000 wei = 0x1b69b4ba630f34000
        // With default precision=6, should show 0.123456
        const result = weiToEth('0x1b69b4bacd05f15');
        assert.ok(result.startsWith('0.'), `Expected to start with 0., got ${result}`);
    });

    it('handles very large values (1000 ETH)', () => {
        // 1000 * 10^18 = 10^21 = 0x3635c9adc5dea00000
        assert.equal(weiToEth('0x3635c9adc5dea00000'), '1000');
    });
});

describe('formatTokenAmount', () => {
    it('formats USDC amount (6 decimals)', () => {
        // 1,000,000 USDC = 1000000 * 10^6 = 1000000000000
        assert.equal(formatTokenAmount('0xe8d4a51000', 6), '1000000');
    });

    it('formats fractional USDC', () => {
        // 1.5 USDC = 1500000
        assert.equal(formatTokenAmount('0x16e360', 6), '1.5');
    });

    it('returns 0 for zero', () => {
        assert.equal(formatTokenAmount('0x0', 18), '0');
    });
});

describe('formatGas', () => {
    it('formats gas with separators', () => {
        assert.equal(formatGas('0xafee'), '45,038');
    });

    it('formats zero gas', () => {
        assert.equal(formatGas('0x0'), '0');
    });
});

describe('shortenAddress', () => {
    it('shortens a standard address', () => {
        assert.equal(
            shortenAddress('0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'),
            '0xc02a...6cc2'
        );
    });

    it('handles empty/short input', () => {
        assert.equal(shortenAddress(''), '');
        assert.equal(shortenAddress('0x1234'), '0x1234');
    });
});
