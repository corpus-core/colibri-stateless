#ifndef patricia_h__
#define patricia_h__

#include "bytes.h"
#include "ssz.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct node node_t;
typedef enum {
  PATRICIA_INVALID      = 0,
  PATRICIA_FOUND        = 1,
  PATRICIA_NOT_EXISTING = 2,
} patricia_result_t;

patricia_result_t patricia_verify(bytes32_t root, bytes_t path, ssz_ob_t proof, bytes_t* last_value);

ssz_ob_t patricia_create_merkle_proof(node_t* root, bytes_t path);
void     patricia_set_value(node_t** root, bytes_t path, bytes_t value);
void     patricia_node_free(node_t* node);
node_t*  patricia_clone_tree(node_t* node);
bytes_t  patricia_get_root(node_t* node);

#ifdef TEST
void patricia_dump(node_t* root);
#endif

#ifdef __cplusplus
}
#endif

#endif
