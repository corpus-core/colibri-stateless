#ifndef HANDLERS_H
#define HANDLERS_H

#include "civetweb.h"

// Handler for test API endpoint
int test_api_handler(struct mg_connection* conn, void* cbdata);

// Handler for Lodestar API endpoint
int lodestar_api_handler(struct mg_connection* conn, void* cbdata);

#endif /* HANDLERS_H */