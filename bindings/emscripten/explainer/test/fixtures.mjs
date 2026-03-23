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
                topics: ['0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef',
                    '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a',
                    '0x000000000000000000000000c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'],
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
                topics: ['0xe1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109c',
                    '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a'],
            },
        },
    ],
    trace: [
        {
            from: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a',
            gas: '0x989680',
            gasUsed: '0x5da6',
            input: '0xd0e30db0',
            output: '0x',
            subtraces: '0x0',
            to: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
            traceAddress: [],
            type: 'CALL',
            value: '0x16345785d8a0000',
        },
    ],
    stateChanges: [
        {
            address: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
            storage: [
                {
                    slot: '0x0242ace4aee0b852ee20a6dadbb8dd2f699da3c4f840b14304b45ac861c0b6c5',
                    previousValue: '0x0000000000000000000000000000000000000000000000000000000000000000',
                    newValue: '0x000000000000000000000000000000000000000000000000016345785d8a0000',
                    slotSource: '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a0000000000000000000000000000000000000000000000000000000000000003',
                },
            ],
            balance: {
                previousValue: '0x01d0e2946ff322c001b43b',
                newValue: '0x01d0e295d3389b1d8bb43b',
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

export const WETH_ABI = [
    { name: 'deposit', type: 'function', inputs: [], outputs: [], payable: true, stateMutability: 'payable' },
    { name: 'withdraw', type: 'function', inputs: [{ name: 'wad', type: 'uint256' }], outputs: [], stateMutability: 'nonpayable' },
    { name: 'balanceOf', type: 'function', inputs: [{ name: '', type: 'address' }], outputs: [{ name: '', type: 'uint256' }], stateMutability: 'view' },
    { name: 'transfer', type: 'function', inputs: [{ name: 'dst', type: 'address' }, { name: 'wad', type: 'uint256' }], outputs: [{ name: '', type: 'bool' }], stateMutability: 'nonpayable' },
    { name: 'approve', type: 'function', inputs: [{ name: 'guy', type: 'address' }, { name: 'wad', type: 'uint256' }], outputs: [{ name: '', type: 'bool' }], stateMutability: 'nonpayable' },
    { name: 'Deposit', type: 'event', inputs: [{ name: 'dst', type: 'address', indexed: true }, { name: 'wad', type: 'uint256', indexed: false }], anonymous: false },
    { name: 'Withdrawal', type: 'event', inputs: [{ name: 'src', type: 'address', indexed: true }, { name: 'wad', type: 'uint256', indexed: false }], anonymous: false },
    { name: 'Transfer', type: 'event', inputs: [{ name: 'src', type: 'address', indexed: true }, { name: 'dst', type: 'address', indexed: true }, { name: 'wad', type: 'uint256', indexed: false }], anonymous: false },
    { name: 'Approval', type: 'event', inputs: [{ name: 'src', type: 'address', indexed: true }, { name: 'guy', type: 'address', indexed: true }, { name: 'wad', type: 'uint256', indexed: false }], anonymous: false },
];

export const REVERTED_TX_RESULT = {
    gasUsed: '0x5208',
    status: '0x0',
    returnValue: '0x08c379a0'
        + '0000000000000000000000000000000000000000000000000000000000000020'
        + '0000000000000000000000000000000000000000000000000000000000000014'
        + '496e73756666696369656e742062616c616e6365000000000000000000000000',
    logs: [],
};

export const UNI_STORAGE_LAYOUT = {
    storage: [
        { slot: '0', type: 't_uint256', astId: 262, label: 'totalSupply', offset: 0, contract: 'Uni.sol:Uni' },
        { slot: '1', type: 't_address', astId: 264, label: 'minter', offset: 0, contract: 'Uni.sol:Uni' },
        { slot: '2', type: 't_uint256', astId: 266, label: 'mintingAllowedAfter', offset: 0, contract: 'Uni.sol:Uni' },
        { slot: '3', type: 't_mapping(t_address,t_mapping(t_address,t_uint96))', astId: 280, label: 'allowances', offset: 0, contract: 'Uni.sol:Uni' },
        { slot: '4', type: 't_mapping(t_address,t_uint96)', astId: 284, label: 'balances', offset: 0, contract: 'Uni.sol:Uni' },
    ],
    types: {
        't_uint256': { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' },
        't_address': { label: 'address', encoding: 'inplace', numberOfBytes: '20' },
        't_uint96': { label: 'uint96', encoding: 'inplace', numberOfBytes: '12' },
        't_mapping(t_address,t_uint96)': { key: 't_address', label: 'mapping(address => uint96)', value: 't_uint96', encoding: 'mapping', numberOfBytes: '32' },
        't_mapping(t_address,t_mapping(t_address,t_uint96))': { key: 't_address', label: 'mapping(address => mapping(address => uint96))', value: 't_mapping(t_address,t_uint96)', encoding: 'mapping', numberOfBytes: '32' },
    },
};
