import Colibri from "@corpus-core/colibri-stateless";

test("Colibri can initialize WASM in Node (RN test project)", async () => {
  const client = new Colibri();
  const methodType = await client.getMethodSupport("eth_getTransactionByHash");
  expect(typeof methodType).toBe("number");
});

