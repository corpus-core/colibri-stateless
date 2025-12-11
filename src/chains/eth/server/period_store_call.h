/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef C4_ETH_PERIOD_STORE_CALL_H
#define C4_ETH_PERIOD_STORE_CALL_H

#include "server.h"
#include <stdbool.h>

/**
 * Handle internal HTTP requests to the period-store.
 *
 * This matches URLs starting with `"period_store/"`, serves data from the local
 * period-store directory, and optionally falls back to a configured master node.
 *
 * @param r Single request context for the in-flight HTTP call.
 * @return true if the handler took ownership of the request, false otherwise.
 */
bool c4_handle_period_store(single_request_t* r);

#endif /* C4_ETH_PERIOD_STORE_CALL_H */
