#ifndef HANDLERS_H
#define HANDLERS_H

#include "civetweb.h"

// Handler for test API endpoint
int test_api_handler(struct mg_connection* conn, void* cbdata);

// Handler for Lodestar API endpoint
int lodestar_api_handler(struct mg_connection* conn, void* cbdata);

// State machine handler that demonstrates callback-based HTTP client usage
int statemachine_handler(struct mg_connection* conn, void* cbdata);

#endif /* HANDLERS_H */