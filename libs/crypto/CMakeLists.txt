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
# add_definitions(-DUSE_HASHER)
set(SRC 
  ecdsa.c
#  address.c
  bignum.c
  rand.c
  secp256k1.c
  memzero.c
  sha3.c
#  hmac_drbg.c
#  base58.c
#  hasher.c
  sha2.c
#  blake256.c
#  blake2b.c
#  groestl.c
#  pbkdf2.c
#  rfc6979.c

  ripemd160.c
#  bip39.c
#  bip39_english.c

  curves.c
#  bip32.c
#  nist256p1.c

)


add_library(crypto STATIC ${SRC} )
target_include_directories(crypto PUBLIC .)

