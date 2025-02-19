#include <stddef.h>
#include <stdint.h>

typedef void proofer_t;
typedef struct {
  uint8_t* data;
  size_t   len;
} bytearray_t;

/**
 * creates a new proofer_ctx_t
 * @param method the method to prove (eth_getTransactionByHash, eth_getBlockByHash, etc.)
 * @param params the params of the method as jsons array like '["0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24"]'
 * @param chain_id the chain id mainnet: 1, sepolia: 11155111, etc.
 * @return a new proofer_ctx_t
 */
proofer_t* create_proofer_ctx(char* method, char* params, uint64_t chain_id);

/**
 * executes the proofer_t and returns the status as json string.
 * the resulting char* - ptr has to be freed by the caller.
 * The json-string has the following format:
 * {
 *  "status": "success" | "error" | "pending",
 *  "error?": "in case of error, the error message",
 *  "result?": "in case of success, the pointer to the bytes of the proof",
 *  "result_len?": "in case of success, the length of the proof",
 *  "requets?": [ // array of requests which need to be fetched before calling this function again.
 *    {
 *      "req_ptr": "pointer of the data request",
 *      "chain_id": "the chain id",
 *      "encoding": "the encoding of the request either json or ssz",
 *      "exclude_mask": "the exclude mask of the request indicating which endpoint to exclude",
 *      "method": "the method of the request (get, post, put, delete)",
 *      "url": "the url of the request",
 *      "payload?": "the payload of the request as json",
 *      "type": "the type of the request either beacon_api, eth_rpc or rest_api"
 *    }
 *  ]
 * }
 */
char* proofer_execute_json_status(proofer_t* ctx);

/**
 * frees the proofer_ctx_t
 * @param ctx the proofer_ctx_t to free
 */
void free_proofer_ctx(proofer_t* ctx);

/**
 * creates the response of the data request by allocating  memory where the data should be copied to.
 * @param req_ptr the pointer to the data request ( as given in the json-string of proofer_execute_json_status)
 * @param len the length of the data to be set as response
 * @param node_index the  index of the node the response came from.
 * @return the pointer to the allocated memory
 */
void req_set_response(void* req_ptr, bytearray_t data, uint16_t node_index);

/**
 * sets the error of the data request
 * @param req_ptr the pointer to the data request ( as given in the json-string of proofer_execute_json_status)
 * @param error the error message
 * @param node_index the  index of the node the error came from.
 */
void req_set_error(void* req_ptr, char* error, uint16_t node_index);

/**
 * verifies the proof created by the proofer_ctx_t
 * @param proof the proof to verify
 * @param proof_len the length of the proof
 * @param method the method of the requested data
 * @param args the args as json array string
 * @param chain_id the chain id
 * @return the result of the verification as json string ( needs to be freed by the caller )
 */
char* verify_proof(uint8_t* proof, uint32_t proof_len, char* method, char* args, uint64_t chain_id);

/**
 * gets the proof from the proofer_t
 * @param ctx the proofer_t
 * @return the proof as bytearray_t
 */
bytearray_t proofer_get_proof(proofer_t* ctx);
