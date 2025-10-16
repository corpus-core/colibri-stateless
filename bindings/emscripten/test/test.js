import { modulePath } from './test_config.js';

async function main() {
    // Dynamically import the module inside the async function
    const C4WModule = await import(modulePath);
    const Colibiri = C4WModule.default; // Assuming C4W is the default export

    const c4 = new Colibiri({ prover: ['http://localhost:8090'] });
    const tx = await c4.rpc('eth_getTransactionByHash', ['0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24']);
    console.log(tx);
}

main().then(console.log).catch(console.error);

