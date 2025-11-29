
pub_hash_mcl_dec = 13505779394729918822222566082782854327042713043290218832594816470244784480251
pub_hash_c_hex = "1ddc001001f1d8cdecd432c67ab65e899ed1b97a5753f3b2116699f736161ffb"
pub_hash_c_dec = int(pub_hash_c_hex, 16)

print(f"MCL PubHash: {pub_hash_mcl_dec}")
print(f"C PubHash:   {pub_hash_c_dec}")

if pub_hash_mcl_dec == pub_hash_c_dec:
    print("PubHash MATCH")
else:
    print("PubHash MISMATCH")

vkey_fr_mcl_dec = 293481887035566064890891717243437097427316870193488568701881297529491892820

# VK_PROGRAM_HASH from zk_verifier_constants.h (first few bytes)
# 00 a6 1a d8 34 7f e8 89 26 1a 35 54 03 ea ef 57 95 d3 d6 ad f0 39 12 6d 55 da 3f e9 aa 9f 2a 54
vkey_c_hex = "00a61ad8347fe889261a355403eaef5795d3d6adf039126d55da3fe9aa9f2a54"
vkey_c_dec = int(vkey_c_hex, 16)

print(f"MCL VKeyFr: {vkey_fr_mcl_dec}")
print(f"C VKeyFr:   {vkey_c_dec}")

if vkey_fr_mcl_dec == vkey_c_dec:
    print("VKeyFr MATCH")
else:
    print("VKeyFr MISMATCH")
