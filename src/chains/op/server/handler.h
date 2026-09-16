#ifndef OP_SERVER_HANDLER_H
#define OP_SERVER_HANDLER_H

#include "server/server.h"
#include "util/json.h"

/** No-op return unless `server->chain_id` is an OP-Stack chain. */
#define OP_HANDLER_CHECK(server)                                            \
  do {                                                                      \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_OP) \
      return;                                                               \
  } while (0)

/** Like `OP_HANDLER_CHECK` but returns `default_return` for non-OP chains. */
#define OP_HANDLER_CHECK_RETURN(server, default_return)                     \
  do {                                                                      \
    if (!(server) || c4_chain_type((server)->chain_id) != C4_CHAIN_TYPE_OP) \
      return (default_return);                                              \
  } while (0)

/** Internal handler: OP preconfirmation storage fetch/store. */
bool c4_handle_preconf(single_request_t* r);

#endif // OP_SERVER_HANDLER_H
