/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file handle_openapi.c
 * @brief HTTP handler for serving the OpenAPI specification
 *
 * Provides the /openapi.yaml endpoint that serves the embedded OpenAPI 3.1.0
 * specification for the Colibri Stateless REST API.
 *
 * The OpenAPI specification is embedded at build time from src/server/openapi.yaml
 * and is always available (no authentication or Web UI flag required).
 */

#include "server.h"
#include <string.h>

// Include the generated OpenAPI YAML (created by CMake from openapi.yaml)
#include "openapi_yaml.h"

/**
 * Handle GET /openapi.yaml - Serve the OpenAPI specification
 *
 * Returns the embedded OpenAPI 3.1.0 specification in YAML format.
 * This endpoint is always available and can be used by documentation tools,
 * API clients, and developers to understand the API structure.
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_openapi(client_t* client) {
  if (strcmp(client->request.path, "/openapi.yaml") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_GET) return false;

  c4_http_respond(client, 200, "text/yaml", bytes((void*) openapi_yaml, strlen(openapi_yaml)));
  return true;
}
