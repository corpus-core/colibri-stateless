#include "c4_assert.h"
#include "proofer/eth_req.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/json.h"
#include "util/patricia.h"
#include "util/ssz.h"
void setUp(void) {
  // Initialisierung vor jedem Test (falls erforderlich)
}

void tearDown(void) {
  // Bereinigung nach jedem Test (falls erforderlich)
}

static json_t read_test(const char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};
  buffer_t      path = {0};
  buffer_add_chars(&path, TESTDATA_DIR "/../eth/TrieTests/");
  buffer_add_chars(&path, filename);

  FILE* file = fopen((char*) path.data.data, "rb");
  buffer_free(&path);
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    TEST_ASSERT_NOT_NULL_MESSAGE(file, "testdata not found");
    return (json_t) {.type = JSON_TYPE_NOT_FOUND, .start = NULL, .len = 0};
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  fclose(file);
  return json_parse((char*) data.data.data);
}

static bytes_t as_bytes(json_t parent, int index, buffer_t* buffer) {
  json_t value = index == -1 ? parent : json_at(parent, index);
  if (value.type == JSON_TYPE_NULL || !value.start)
    return bytes(NULL, 0);
  if (value.start[0] == '0' && value.start[1] == 'x')
    return json_as_bytes(value, buffer);
  return bytes((uint8_t*) value.start + 1, value.len - 2);
}

static void run_test(const char* file, const char* testname) {
  json_t   data = read_test(file);
  bytes_t  name;
  buffer_t tmp1 = {0};
  buffer_t tmp2 = {0};

  json_for_each_property(data, test, name) {
    if (testname != NULL && strncmp(testname, (char*) name.data, name.len)) continue;
    tmp1.data.len = 0;
    buffer_append(&tmp1, name);
    buffer_add_chars(&tmp1, "\n");
    printf("### run %s\n", (char*) tmp1.data.data);

    json_t    in            = json_get(test, "in");
    bytes32_t expected_root = {0};
    buffer_t  buffer_root   = stack_buffer(expected_root);
    json_get_bytes(test, "root", &buffer_root);

    // create
    node_t* root = NULL;
    if (in.type == JSON_TYPE_ARRAY) {
      json_for_each_value(in, item) {
        bytes_t key   = as_bytes(item, 0, &tmp1);
        bytes_t value = as_bytes(item, 1, &tmp2);
        patricia_set_value(&root, key, value);
      }
    }
    else {
      json_for_each_property(in, item, name) {
        bytes_t value = as_bytes(item, -1, &tmp2);
        patricia_set_value(&root, name, value);
      }
    }
    bytes_t root_hash = patricia_get_root(root);

    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash.data, 32, "invalid root");
    //    patricia_dump(root);
    // printf("result: %d\n", memcmp(expected_root, root_hash.data, 32));
    patricia_node_free(root);
  }

  buffer_free(&tmp1);
  buffer_free(&tmp2);
  free((void*) data.start);
}

void test_receipt_tree() {
  json_t receipts = read_test("block_receipts.json");
  json_t block    = read_test("block.json");

  node_t*   root         = NULL;
  bytes32_t tmp          = {0};
  buffer_t  buf          = stack_buffer(tmp);
  buffer_t  receipts_buf = {0};
  buffer_t  file         = {0};

  json_for_each_value(receipts, r) {

    bytes_t key   = c4_eth_create_tx_path(json_get_uint32(r, "transactionIndex"), &buf);
    bytes_t value = c4_serialize_receipt(r, &receipts_buf);
    bprintf(&file, "0x%x : 0x%x\n", key, value);

    patricia_set_value(&root, key, value);
  }

  bytes_write(file.data, fopen("receipts.txt", "w"), true);

  bytes_t expected_root = json_get_bytes(block, "receiptsRoot", &buf);

  print_hex(stderr, expected_root, "expected   receipts_root : 0x", "\n");
  print_hex(stderr, patricia_get_root(root), "calculated receipts_root : 0x", "\n");

  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root.data, patricia_get_root(root).data, 32, "invalid root");

  //  ssz_ob_t proof = patricia_create_merkle_proof(root, c4_eth_create_tx_path(tx_index, &buf));

  patricia_node_free(root);
  buffer_free(&buf);
  buffer_free(&receipts_buf);
  free((void*) receipts.start);
  free((void*) block.start);
}

void test_basic() {
  run_test("trietest.json", "insert-middle-leaf");
  run_test("trietest.json", "branch-value-update");
  // run_test("trieanyorder.json", "smallValues"); // no leak
  run_test("trieanyorder.json", NULL); // leaks

  // run_test("trieanyorder.json", "singleItem");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_receipt_tree);
  //  RUN_TEST(test_basic);
  return UNITY_END();
}