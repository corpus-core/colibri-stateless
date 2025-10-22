/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file handle_config.c
 * @brief HTTP handler for server configuration management
 *
 * Provides endpoints for reading and updating server configuration:
 * - GET /config - Returns current configuration as JSON with metadata
 * - POST /config - Updates configuration
 * - GET /config.html - Serves the configuration UI
 *
 * This handler dynamically builds the configuration from the registered
 * parameters in configure.c, ensuring consistency and automatic updates
 * when new parameters are added.
 */

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the generated HTML (created by CMake from web_ui/config.html)
#include "config_html.h"

/**
 * Handle GET /config - Return current configuration as JSON with metadata
 *
 * Returns a JSON object with a "parameters" array containing:
 * - name: parameter name (e.g., "port")
 * - env: environment variable name (e.g., "PORT")
 * - description: human-readable description
 * - type: "int", "string", or "key"
 * - value: current value (not included for "key" type)
 * - min, max: range for int parameters
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_get_config(client_t* client) {
  if (strcmp(client->request.path, "/config") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_GET) return false;

  // Security: Check if Web-UI is enabled
  if (!http_server.web_ui_enabled) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Web UI is disabled\", \"message\": \"Enable with WEB_UI_ENABLED=1 or -u flag\"}");
    c4_http_respond(client, 403, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  buffer_t              data = {0};
  int                   param_count;
  const config_param_t* params = c4_get_config_params(&param_count);

  bprintf(&data, "{\n");
  bprintf(&data, "  \"parameters\": [\n");

  for (int i = 0; i < param_count; i++) {
    const config_param_t* p = &params[i];

    bprintf(&data, "    {\n");
    bprintf(&data, "      \"name\": \"%S\",\n", p->arg_name);           // %S = JSON-escaped, quotes added
    bprintf(&data, "      \"env\": \"%S\",\n", p->name);                // %S = JSON-escaped, quotes added
    bprintf(&data, "      \"description\": \"%S\",\n", p->description); // %S = JSON-escaped, quotes added

    // Type
    const char* type_str = p->type == CONFIG_PARAM_INT      ? "int"
                           : p->type == CONFIG_PARAM_STRING ? "string"
                                                            : "key";
    bprintf(&data, "      \"type\": \"%S\"", type_str);

    // Value (skip for keys)
    if (p->type != CONFIG_PARAM_KEY) {
      bprintf(&data, ",\n");
      bprintf(&data, "      \"value\": ");

      switch (p->type) {
        case CONFIG_PARAM_INT: {
          int* val = (int*) p->value_ptr;
          bprintf(&data, "%d", *val);
          break;
        }
        case CONFIG_PARAM_STRING: {
          char** val = (char**) p->value_ptr;
          bprintf(&data, "\"%S\"", *val ? *val : ""); // %S = JSON-escaped, quotes added
          break;
        }
        case CONFIG_PARAM_KEY:
          // Already handled above
          break;
      }
    }

    // Min/Max for int parameters
    if (p->type == CONFIG_PARAM_INT) {
      bprintf(&data, ",\n");
      bprintf(&data, "      \"min\": %d,\n", p->min);
      bprintf(&data, "      \"max\": %d\n", p->max);
    }
    else {
      bprintf(&data, "\n");
    }

    bprintf(&data, "    }%s\n", i < param_count - 1 ? "," : "");
  }

  bprintf(&data, "  ]\n");
  bprintf(&data, "}\n");

  c4_http_respond(client, 200, "application/json", data.data);
  buffer_free(&data);
  return true;
}

/**
 * Handle POST /config - Update configuration
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_post_config(client_t* client) {
  if (strcmp(client->request.path, "/config") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_POST) return false;

  // Security: Check if Web-UI is enabled
  if (!http_server.web_ui_enabled) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Web UI is disabled\", \"message\": \"Enable with WEB_UI_ENABLED=1 or -u flag\"}");
    c4_http_respond(client, 403, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  // TODO: Parse JSON body and update configuration
  // This would require:
  // 1. JSON parsing (or simple hand-rolled parser)
  // 2. Validation of new values
  // 3. Writing to config file
  // 4. Optionally restarting/reloading server

  buffer_t data = {0};
  bprintf(&data, "{\"error\": \"Configuration updates not yet implemented\", \"message\": \"This feature will be available in a future update\"}");
  c4_http_respond(client, 501, "application/json", data.data);
  buffer_free(&data);
  return true;
}

/**
 * Handle GET /config.html - Serve the configuration UI
 *
 * Serves the embedded HTML (generated from web_ui/config.html at build time)
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_config_ui(client_t* client) {
  if (strcmp(client->request.path, "/config.html") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_GET) return false;

  // Security: Check if Web-UI is enabled
  if (!http_server.web_ui_enabled) {
    const char* error_html =
        "<!DOCTYPE html><html><head><title>Web UI Disabled</title></head>"
        "<body style='font-family: sans-serif; text-align: center; padding: 50px;'>"
        "<h1>🔒 Web UI Disabled</h1>"
        "<p>The configuration web interface is currently disabled for security reasons.</p>"
        "<p>To enable it, add <code>WEB_UI_ENABLED=1</code> to your config file or use the <code>-u</code> flag.</p>"
        "<p><strong>Warning:</strong> Only enable this on local/trusted networks!</p>"
        "</body></html>";
    c4_http_respond(client, 403, "text/html", bytes((void*) error_html, strlen(error_html)));
    return true;
  }

  c4_http_respond(client, 200, "text/html", bytes((void*) config_html, strlen(config_html)));
  return true;
}
