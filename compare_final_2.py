
# From debug_output_cross_check_2.txt

# MCL FinalExp(Eth Miller) - First component (decimal)
mcl_c0_c0_c0 = 17264119758069723980713015158403419364912226240334615592005620718956030922389

# My FE y (final) - First component (hex bytes)
# 1bc222c5ca481bea06071845aff6ecd7037b0ddeb40d4151be33b1a5b0d8e045 
# Wait, the hex string in line 13 is 12 blocks.
# First block: 262b253feda94cfe0da01bde280a3ed6f87e5feb898578b55e1f63739d870e95
# Wait, I am reading Line 13.
# "262b253feda94cfe0da01bde280a3ed6f87e5feb898578b55e1f63739d870e95"

my_c0_c0_c0_hex = "262b253feda94cfe0da01bde280a3ed6f87e5feb898578b55e1f63739d870e95"
my_c0_c0_c0 = int(my_c0_c0_c0_hex, 16)

print(f"MCL val: {mcl_c0_c0_c0}")
print(f"My val:  {my_c0_c0_c0}")

if mcl_c0_c0_c0 == my_c0_c0_c0:
    print("MATCH")
else:
    print("MISMATCH")


