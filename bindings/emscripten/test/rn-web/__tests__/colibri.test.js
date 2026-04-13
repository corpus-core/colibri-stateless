test("Colibri can initialize WASM in Node (RN test project)", async () => {
  // Use CommonJS resolution in Jest to ensure we load the CJS build output.
  // This avoids ESM-only syntax (e.g. static class blocks) in toolchains that
  // don't support it in the Jest transform pipeline.
  // eslint-disable-next-line global-require
  const { default: Colibri } = require("@corpus-core/colibri-stateless");
  const client = new Colibri();
  const methodType = await client.getMethodSupport("eth_getTransactionByHash");
  expect(typeof methodType).toBe("number");
});

