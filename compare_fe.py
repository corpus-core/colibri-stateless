
mcl_final_from_eth = 21152491327768951736604073769314087391180555833863737987483494328983300624055
my_final = 0x2ec3e2c55a6d3d58f91680427627e3b7359e6d74531d9bff8f2d6084595c4eb7

print(f"MCL FE(Eth Miller) last: {hex(mcl_final_from_eth)}")
print(f"My FE last:              {hex(my_final)}")

if mcl_final_from_eth == my_final:
    print("MATCH!")
else:
    print("MISMATCH")


