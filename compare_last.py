
mcl_last = 440796048150724096437130979851431985500142692666486515369083499585648077975
print(f"MCL last: {hex(mcl_last)}")

# My output last component (from log)
# 2ec3e2c55a6d3d58f91680427627e3b7359e6d74531d9bff8f2d6084595c4eb7
my_last = 0x2ec3e2c55a6d3d58f91680427627e3b7359e6d74531d9bff8f2d6084595c4eb7
print(f"My last:  {hex(my_last)}")

if mcl_last == my_last:
    print("MATCH!")
else:
    print("MISMATCH")


