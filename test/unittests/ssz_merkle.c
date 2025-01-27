// datei: test_addiere.c
#include "c4_assert.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/ssz.h"
void setUp(void) {
  // Initialisierung vor jedem Test (falls erforderlich)
}

void tearDown(void) {
  // Bereinigung nach jedem Test (falls erforderlich)
}

void test_hash_root() {

  ssz_def_t TEST_TYPE[] = {
      SSZ_UINT8("a"),
      SSZ_UINT8("b"),
      SSZ_UINT8("c"),
  };

  ssz_def_t TEST_ROOT[] = {
      SSZ_UINT8("dummy"),
      SSZ_CONTAINER("sub", TEST_TYPE),
  };

  ssz_def_t TEST_TYPE_CONTAINER = SSZ_CONTAINER("TEST_ROOT", TEST_ROOT);
  uint8_t   ssz_data[]          = {1, 2, 3, 4};
  bytes32_t root                = {0};
  ssz_ob_t  res                 = ssz_ob(TEST_TYPE_CONTAINER, bytes(ssz_data, sizeof(ssz_data)));

  ssz_hash_tree_root(res, root);
  const char* path[] = {"sub", "a"};
  uint32_t    gindex;
  buffer_t    proof = {0};
  ssz_create_proof(res, (char**) path, sizeof(path) / sizeof(path[0]), &proof, &gindex);

  // ssz_dump(stdout, res, true, 0);
  // print_hex(stdout, res.bytes, "DATA: 0x", "\n");
  // print_hex(stdout, bytes(root, 32), "Hashroot: 0x", "\n");
  // for (int i = 0; i < proof.data.len; i += 32) print_hex(stdout, bytes(proof.data.data + i, 32), "Proof: 0x", "\n");

  bytes32_t root2 = {0};
  bytes32_t leaf  = {0};
  leaf[0]         = 2;
  ssz_verify_merkle_proof(proof.data, leaf, 12, root2);
  //  printf("gindex: %d\n", gindex);
  //  print_hex(stdout, bytes(root2, 32), "ProotRoot: 0x", "\n");
  TEST_ASSERT_EQUAL_MESSAGE(12, gindex, "invalid gindex");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, root2, 32, "root hash must be the same after merkle proof");
  ASSERT_HEX_STRING_EQUAL("0xdf0a32672e8c927cfc3acd778121417e0597a8042d0994b6d069d16f66b62081", root, 32, "invalid hash tree root");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hash_root);

  return UNITY_END();
}