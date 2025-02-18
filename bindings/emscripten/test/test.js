import C4W from "../../../build/emscripten/index.js";

const c4w = new C4W();

async function main() {
    const method = "eth_getTransactionByHash";
    const args = ['0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24'];


    const proof = await c4w.createProof(method, args);
    const result = await c4w.verifyProof(method, args, proof);
    console.log(result);
}

main().then(console.log).catch(console.error);

