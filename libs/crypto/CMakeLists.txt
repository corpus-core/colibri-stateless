string(REGEX MATCH "^[0-9]+" MAJOR_VERSION ${CMAKE_C_COMPILER_VERSION})
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MAJOR_VERSION GREATER 12)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-null-pointer-subtraction -Wno-implicit-function-declaration")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MAJOR_VERSION GREATER 15)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-array-parameter")
endif()

# Conditionally set flags based on the compiler
if(NOT MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-#pragma-messages")
endif()

add_definitions(-DAES_128)

set(SRC 
  ecdsa.c
  address.c
  bignum.c
  rand.c
  secp256k1.c
  memzero.c
  sha3.c
  hmac_drbg.c
  ripemd160.c
  base58.c
  hasher.c
  sha2.c
  blake256.c
  blake2b.c
  groestl.c
#  pbkdf2.c
  rfc6979.c


#  bip39.c
#  bip39_english.c

  curves.c
#  bip32.c
#  nist256p1.c

)

if (ED25519) 
  set(SRC ${SRC} 
    ed25519-donna/curve25519-donna-32bit.c
    ed25519-donna/curve25519-donna-helpers.c
    ed25519-donna/curve25519-donna-scalarmult-base.c
    ed25519-donna/ed25519-donna-32bit-tables.c
    ed25519-donna/ed25519-donna-basepoint-table.c
    ed25519-donna/ed25519-donna-impl-base.c
    ed25519-donna/ed25519-keccak.c
    ed25519-donna/ed25519-sha3.c
    ed25519-donna/ed25519.c
    ed25519-donna/modm-donna-32bit.c
  )
 ADD_DEFINITIONS(-DUSE_BIP32_25519_CURVES=1)
else()
  ADD_DEFINITIONS(-DUSE_BIP32_25519_CURVES=0)
endif()


if (NOT ESP_IDF)
    set(SRC ${SRC} 
    hmac.c
    aes/aes_modes.c
    aes/aescrypt.c
    aes/aeskey.c
    aes/aestab.c    
    )
endif()

add_library(crypto STATIC ${SRC} )

