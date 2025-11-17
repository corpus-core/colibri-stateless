#ifndef OP_SERVER_HANDLER_H
#define OP_SERVER_HANDLER_H

#include "server/server.h"
#include "util/json.h"

// Helper macro to check if this handler should be active for the given server
#define OP_HANDLER_CHECK(server)                                            \
  do {                                                                      \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_OP) \
      return;                                                               \
  } while (0)

// Helper macro for functions that return a value
#define OP_HANDLER_CHECK_RETURN(server, default_return)                     \
  do {                                                                      \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_OP) \
      return (default_return);                                              \
  } while (0)

bool c4_handle_preconf(single_request_t* r);
#endif // ETH_SERVER_HANDLER_H
