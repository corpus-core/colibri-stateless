// Import the main module entry point using the alias configured in webpack.config.js
import Colibri from '@c4w/index.js';

// Function to run the test logic
async function runTest() {
    // Get the output element *after* the DOM is loaded
    const outputDiv = document.getElementById('output');


    if (!outputDiv) {
        console.error("Failed to find the 'output' div!");
        return;
    }

    outputDiv.textContent = 'Loading WASM and initializing Colibri...';
    try {
        // Instantiate the client
        // The underlying WASM module loading should be handled by the Colibri class/index.js wrapper
        let client = new Colibri({
            prover: ['https://mainnet1.colibri-proof.tech']
        });
        outputDiv.textContent = 'Colibri client instantiated. Calling verifyProof...';

        // Call the verifyProof method
        // Note: Ensure verifyProof is an async method or returns a Promise if it interacts with WASM asynchronously
        //        let result = await client.verifyProof('web3_clientVersion', [], new Uint8Array());
        let result = await client.rpc('eth_getBlockByNumber', ['latest', false]);

        // Display the result
        outputDiv.textContent = `verifyProof result: ${JSON.stringify(result)}`;
        console.log('verifyProof result:', result);

    } catch (err) {
        outputDiv.textContent = `Error during Colibri test: ${err}`;
        console.error(err);
    }
}

// Wait for the DOM to be fully loaded before running the test
document.addEventListener('DOMContentLoaded', () => {
    runTest();
}); 