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

void test_block_body() {
  bytes_t data = read_testdata("body.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "body.ssz not found");
  ssz_ob_t  signed_beacon_block = ssz_ob(SIGNED_BEACON_BLOCK_CONTAINER, data);
  ssz_ob_t  block               = ssz_get(&signed_beacon_block, "message");
  bytes32_t blockhash           = {0};
  ssz_hash_tree_root(block, blockhash);
  ASSERT_HEX_STRING_EQUAL("0x4dbac2cc64863d5b59244662993ef74f8635086b4096a9e29eef0cbc794f8841", blockhash, 32, "invalid blockhash");

  // create state proof
  ssz_ob_t body = ssz_get(&block, "body");
  TEST_ASSERT_NOT_NULL_MESSAGE(body.bytes.data, "body not found");
  gindex_t gindex = ssz_gindex(body.def, 2, "executionPayload", "stateRoot");
  bytes_t  proof  = ssz_create_proof(body, blockhash, gindex);
  TEST_ASSERT_EQUAL_UINT32(802, gindex);

  // verify proof
  ssz_ob_t  exec_state = ssz_get(&body, "executionPayload");
  bytes_t   state_root = ssz_get(&exec_state, "stateRoot").bytes;
  bytes32_t root       = {0};
  bytes32_t body_root  = {0};
  ssz_hash_tree_root(body, body_root);
  ssz_verify_single_merkle_proof(proof, state_root.data, 802, root);

  TEST_ASSERT_EQUAL_MESSAGE(32, state_root.len, "invalid stateroot length");
  ASSERT_HEX_STRING_EQUAL("0xc255ec5d008f5c8bc009e6f7aff0dd831245efd6a3657c1f91d7c4c44613df12",
                          state_root.data, 32, "invalid stateroot");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(body_root, root, 32, "root hash must be the same after merkle proof");
  TEST_ASSERT_EQUAL_MESSAGE(9, proof.len / 32, "invalid prooflength");
  free(proof.data);
}

void test_hash_root() {

  ssz_def_t TEST_SUB[] = {
      SSZ_UINT8("a"),
      SSZ_UINT8("b"),
      SSZ_UINT8("c"),
  };

  ssz_def_t TEST_ROOT[] = {
      SSZ_UINT8("count"),
      SSZ_CONTAINER("sub", TEST_SUB),
  };

  TEST_ASSERT_EQUAL(7, ssz_add_gindex(3, 3));
  TEST_ASSERT_EQUAL(4, ssz_add_gindex(2, 2));
  TEST_ASSERT_EQUAL(14, ssz_add_gindex(7, 2));

  ssz_def_t TEST_TYPE_CONTAINER = SSZ_CONTAINER("TEST_ROOT", TEST_ROOT);
  uint8_t   ssz_data[]          = {1, 2, 3, 4};
  bytes32_t root                = {0};
  ssz_ob_t  res                 = ssz_ob(TEST_TYPE_CONTAINER, bytes(ssz_data, sizeof(ssz_data)));

  ssz_hash_tree_root(res, root);
  gindex_t gindex = ssz_gindex(res.def, 2, "sub", "a");
  bytes_t  proof  = ssz_create_proof(res, root, gindex);

  // ssz_dump(stdout, res, true, 0);
  // print_hex(stdout, res.bytes, "DATA: 0x", "\n");
  // print_hex(stdout, bytes(root, 32), "Hashroot: 0x", "\n");
  // for (int i = 0; i < proof.data.len; i += 32) print_hex(stdout, bytes(proof.data.data + i, 32), "Proof: 0x", "\n");

  bytes32_t root2 = {0};
  bytes32_t leaf  = {0};
  leaf[0]         = 2;
  //  ssz_verify_merkle_proof(proof.data, leaf, 12, root2);
  ssz_verify_single_merkle_proof(proof, leaf, 12, root2);
  free(proof.data);

  //  printf("gindex: %d\n", gindex);
  //  print_hex(stdout, bytes(root2, 32), "ProotRoot: 0x", "\n");
  TEST_ASSERT_EQUAL_MESSAGE(12, gindex, "invalid gindex");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, root2, 32, "root hash must be the same after merkle proof");
  ASSERT_HEX_STRING_EQUAL("0xdf0a32672e8c927cfc3acd778121417e0597a8042d0994b6d069d16f66b62080", root, 32, "invalid hash tree root");

  // create multproof
  bytes_t multi_proof = ssz_create_multi_proof(res, root, 3,
                                               ssz_gindex(res.def, 1, "count"),
                                               ssz_gindex(res.def, 2, "sub", "a"),
                                               ssz_gindex(res.def, 2, "sub", "b"));

  uint8_t leafes[96] = {0};
  leafes[0]          = 1;
  leafes[32]         = 2;
  leafes[64]         = 3;

  gindex_t gindexes[] = {
      ssz_gindex(res.def, 1, "count"),
      ssz_gindex(res.def, 2, "sub", "a"),
      ssz_gindex(res.def, 2, "sub", "b")};

  ssz_verify_multi_merkle_proof(multi_proof, bytes(leafes, sizeof(leafes)), gindexes, root2);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, root2, 32, "root hash must be the same after merkle proof");
  free(multi_proof.data);
}

void test_hash_body() {
  bytes_t data = read_testdata("body_11038724.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "body_11038724.ssz not found");
  ssz_ob_t  block = ssz_ob(BEACON_BLOCK_BODY_CONTAINER, data);
  bytes32_t root  = {0};
  ssz_hash_tree_root(block, root);
  ASSERT_HEX_STRING_EQUAL("ef0d785cb18cb409d4ec8ae1a2f815542b66425716623b16192389e38af32ba7", root, 32, "invalid blockhash");
  free(data.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hash_body);
  RUN_TEST(test_hash_root);
  RUN_TEST(test_block_body);
  return UNITY_END();
}