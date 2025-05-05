#ifndef __C4_CURL_H__
#define __C4_CURL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../../src/proofer/proofer.h"

void curl_fetch(data_request_t* req);
void curl_fetch_all(c4_state_t* state);
void curl_set_config(json_t config);
void curl_set_chain_store(const char* dir);
#ifdef TEST
char* curl_set_test_dir(const char* dir);
void  curl_set_cache_dir(const char* dir);
#endif

#ifdef __cplusplus
}
#endif

#endif