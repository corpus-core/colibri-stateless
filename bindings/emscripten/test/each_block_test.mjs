// Create bindings/emscripten/test/basic.test.ts
import test from 'node:test';
import assert from 'node:assert';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default; // Assuming Colibri is the default export
const supported_chains = ['mainnet', 'gnosis', 'chiado', 'sepolia'];

let chains = process.argv.slice(2);
let options = chains.filter(chain => !supported_chains.includes(chain));
chains = chains.filter(chain => supported_chains.includes(chain));
if (chains.length == 0) chains = ['mainnet', 'gnosis'];

for (const chain of chains) {
    let conf = { chainId: chain, debug: true }
    if (options.includes('-l')) conf.prover = ['http://localhost:8090'];
    const c4 = new Colibri(conf);
    const state = {}
    let r = await c4.request({ method: 'eth_subscribe', params: ['newHeads'] });
    c4.on('message', (msg) => {
        if (msg.type == 'eth_subscription' && msg?.data?.subscription == r) {
            const block = msg.data.result;
            test_block(block, chain, c4, state).catch(err => {
                console.log(`Error testing block ${block.number} on chain ${chain}`);
                console.error(err);
            });
        }
    });
}

function check(a, b) {
    let ja = JSON.stringify(a);
    let jb = JSON.stringify(b);
    if (ja != jb) {
        console.log(`${JSON.stringify(a, null, 2)} != ${JSON.stringify(b, null, 2)}`);
        return false;
    }
    return true;
}

async function test_block(block, chain, c4, state) {

    const start = performance.now();
    state.start = start;

    if (chain == 'mainnet') {
        c4.request({
            method: 'eth_call', params: [{
                to: "0xdac17f958d2ee523a2206206994597c13d831ec7",
                data: "0x70a08231000000000000000000000000Eff6cb8b614999d130E537751Ee99724D01aA167"
            }, 'latest']
        }).then(result => {
            console.log(`     ## ${chain} :: eth_call:  ${Math.round(performance.now() - start)} ms`);
        }, error => {
            console.log(`     ## ${chain} :: eth_call error:`, error);
        });

    }



    let handled = 0
    for (let tx of block.transactions) {
        if (state.start != start) break
        const [tx_details, tx_receipt] = await Promise.all([
            c4.request({ method: 'eth_getTransactionByHash', params: [tx.hash] }),
            c4.request({ method: 'eth_getTransactionReceipt', params: [tx.hash] })
        ]);
        handled++;

        console.log(`     ## ${chain} :: ${parseInt(block.number)} : ${handled} of ${block.transactions.length} `);

        if (!check(tx_details, tx)) {
            console.log(`ERROR:Transaction ${tx.hash} mismatch`);
        }
        if (!check(tx_receipt.transactionHash, tx.hash)) {
            console.log(`ERROR:Transaction receipt ${tx.hash} mismatch : ${JSON.stringify(tx_receipt, null, 2)}`);
        }
    }
    const duration = performance.now() - start;
    console.log(`:: ${chain} :: ${parseInt(block.number)} ${handled} of ${block.transactions.length} checked in ${(duration / handled).toFixed(2)}ms per tx`);

}






