export const WETH_DEPOSIT_RESULT = {
    gasUsed: '0xafee',
    status: '0x1',
    returnValue: '0x',
    logs: [
        {
            inputs: [
                { name: 'from', type: 'address', value: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a' },
                { name: 'to', type: 'address', value: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2' },
                { name: 'value', type: 'uint256', value: '0x16345785d8a0000' },
            ],
            name: 'Transfer',
            raw: {
                address: '0x0000000000000000000000000000000000000000',
                data: '0x000000000000000000000000000000000000000000000000016345785d8a0000',
                topics: ['0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef'],
            },
        },
        {
            inputs: [
                { name: 'dst', type: 'address', value: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a' },
                { name: 'wad', type: 'uint256', value: '0x16345785d8a0000' },
            ],
            name: 'Deposit',
            raw: {
                address: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
                data: '0x000000000000000000000000000000000000000000000000016345785d8a0000',
                topics: ['0xe1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109c'],
            },
        },
    ],
};

export const TX_PARAMS = {
    to: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
    from: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a',
    value: '0x16345785d8a0000',
    data: '0xd0e30db0',
};
