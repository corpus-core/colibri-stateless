#  USDC-Token balanceof auf sepolia
colibri-verifier  -l 4 -s tmp -c sepolia -o proof.ssz eth_call '{"to": "0x1c7D4B196Cb0C7B01d743Fbc6116a902379C7238","data": "0x70a08231000000000000000000000000208AA722Aca42399eaC5192EE778e4D42f4E5De3"}' latest

#  USDT-Token balanceof auf mainnet
colibri-verifier  -l 4 -s tmp -o proof.ssz eth_call '{"to": "0xdac17f958d2ee523a2206206994597c13d831ec7","data": "0x70a08231000000000000000000000000Eff6cb8b614999d130E537751Ee99724D01aA167"}' latest


