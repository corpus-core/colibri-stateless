#include "chains.h"
#include <stdlib.h>
#include <string.h>

chain_type_t c4_chain_type(chain_id_t chain_id) {
  switch (chain_id) {
    case C4_CHAIN_MAINNET:
      return C4_CHAIN_TYPE_ETHEREUM;
    default:
      return C4_CHAIN_TYPE_ETHEREUM;
  }
}
