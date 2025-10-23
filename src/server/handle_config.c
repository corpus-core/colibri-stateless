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

#include "../util/json.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the generated HTML (created by CMake from web_ui/config.html)
#include "config_html.h"

/**
 * Check if Web UI is enabled and send error page if disabled
 *
 * @param client HTTP client connection
 * @return true if Web UI is disabled (request handled), false if enabled (continue processing)
 */
static bool check_web_ui_access(client_t* client) {
  if (http_server.web_ui_enabled) {
    return false; // Web UI is enabled, continue processing
  }

  // Web UI is disabled - send error page
  const char* error_html =
      "<!DOCTYPE html><html><head><title>Web UI Disabled</title></head>"
      "<body style='font-family: sans-serif; text-align: center; padding: 50px;'>"
      "<h1>&#128274; Web UI Disabled</h1>"
      "<p>The configuration web interface is currently disabled for security reasons.</p>"
      "<p>To enable it, add <code>WEB_UI_ENABLED=1</code> to your config file or use the <code>-u</code> flag.</p>"
      "<p><strong>Warning:</strong> Only enable this on local/trusted networks!</p>"
      "</body></html>";
  c4_http_respond(client, 403, "text/html", bytes((void*) error_html, strlen(error_html)));
  return true; // Request handled
}

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
  if (check_web_ui_access(client)) return true;

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
 * Expects JSON body with "parameters" array containing objects with "env" and "value" fields.
 * Validates values against parameter definitions and writes to config file.
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_post_config(client_t* client) {
  if (strcmp(client->request.path, "/config") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_POST) return false;

  // Security: Check if Web-UI is enabled
  if (check_web_ui_access(client)) return true;

  // Check if config file path is available
  if (!c4_get_config_file_path()) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"No config file\", \"message\": \"Server was started without a config file. Cannot save changes.\"}");
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  // Parse and validate JSON request
  // Expected: {"parameters": [{"env": "PORT", "value": "8090"}, ...]}
  if (!client->request.payload || client->request.payload_len == 0) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Empty body\", \"message\": \"Request body is required\"}");
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  // Parse JSON
  json_t root = json_parse((const char*) client->request.payload);
  if (root.type != JSON_TYPE_OBJECT) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Invalid JSON\", \"message\": \"Request body must be valid JSON object\"}");
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    return true;
  }
  /*
  // Validate JSON structure
  const char* validation_error = json_validate(root, "{parameters:[{env:string,value:*}]}", "request");
  if (validation_error) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Invalid JSON format\", \"message\": \"%s\"}", validation_error);
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    free((void*) validation_error);
    return true;
  }
  */

  // Get parameters array
  json_t params_array = json_get(root, "parameters");
  if (params_array.type != JSON_TYPE_ARRAY) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Invalid format\", \"message\": \"Missing 'parameters' array\"}");
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  // Get parameter definitions for validation
  int                   param_count;
  const config_param_t* params = c4_get_config_params(&param_count);

  // Build update string (KEY=VALUE\n format)
  buffer_t updates       = {0};
  buffer_t errors        = {0};
  buffer_t temp_buffer   = {0};
  int      updated_count = 0;

  // Iterate over parameters array
  json_for_each_value(params_array, param_obj) {
    if (param_obj.type != JSON_TYPE_OBJECT) continue;

    // Get env name and value
    json_t env_json   = json_get(param_obj, "env");
    json_t value_json = json_get(param_obj, "value");

    if (env_json.type == JSON_TYPE_NOT_FOUND || value_json.type == JSON_TYPE_NOT_FOUND) {
      continue;
    }

    // Extract env name (copy to avoid use-after-free)
    char* env_name = json_as_string(env_json, &temp_buffer);
    if (!env_name || strlen(env_name) == 0) {
      buffer_free(&temp_buffer);
      continue;
    }

    // Make a copy of env_name since we'll free temp_buffer
    char env_name_copy[128];
    strncpy(env_name_copy, env_name, sizeof(env_name_copy) - 1);
    env_name_copy[sizeof(env_name_copy) - 1] = '\0';
    buffer_free(&temp_buffer);

    // Find parameter definition for validation
    const config_param_t* param = NULL;
    for (int j = 0; j < param_count; j++) {
      if (strcmp(params[j].name, env_name_copy) == 0) {
        param = &params[j];
        break;
      }
    }

    if (!param) {
      bprintf(&errors, "Unknown parameter: %s. ", env_name_copy);
      continue;
    }

    // Skip KEY parameters (sensitive)
    if (param->type == CONFIG_PARAM_KEY) {
      bprintf(&errors, "Cannot update sensitive parameter: %s. ", env_name_copy);
      continue;
    }

    // Extract and validate value based on type
    if (param->type == CONFIG_PARAM_INT) {
      int val = (int) json_as_uint64(value_json);
      if (val < param->min || val > param->max) {
        bprintf(&errors, "Parameter %s value %d out of range [%d, %d]. ", env_name_copy, val, param->min, param->max);
        continue;
      }
      bprintf(&updates, "%s=%d\n", env_name_copy, val);
    }
    else {
      // String type
      char* value_str = json_as_string(value_json, &temp_buffer);
      if (!value_str) {
        bprintf(&errors, "Invalid value for parameter: %s. ", env_name_copy);
        buffer_free(&temp_buffer);
        continue;
      }
      bprintf(&updates, "%s=%s\n", env_name_copy, value_str);
      buffer_free(&temp_buffer);
    }

    updated_count++;
  }

  // Check if there were errors
  if (errors.data.data && strlen((const char*) errors.data.data) > 0) {
    buffer_t response = {0};
    bprintf(&response, "{\"error\": \"Validation failed\", \"message\": \"%S\"}", errors.data.data);
    c4_http_respond(client, 400, "application/json", response.data);
    buffer_free(&response);
    buffer_free(&errors);
    buffer_free(&updates);
    return true;
  }

  if (updated_count == 0) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"No updates\", \"message\": \"No valid parameters found in request\"}");
    c4_http_respond(client, 400, "application/json", data.data);
    buffer_free(&data);
    buffer_free(&updates);
    return true;
  }

  // Save to config file
  int result = c4_save_config_file((const char*) updates.data.data);
  buffer_free(&updates);

  if (result != 0) {
    buffer_t data = {0};
    bprintf(&data, "{\"error\": \"Save failed\", \"message\": \"Could not write config file\"}");
    c4_http_respond(client, 500, "application/json", data.data);
    buffer_free(&data);
    return true;
  }

  // Success - restart required
  buffer_t data = {0};
  bprintf(&data, "{\"success\": true, \"restart_required\": true, \"message\": \"Configuration saved. Restart server to apply changes.\", \"updated_count\": %d}", updated_count);
  c4_http_respond(client, 200, "application/json", data.data);
  buffer_free(&data);
  return true;
}

/**
 * Handle POST /api/restart - Gracefully restart the server
 *
 * Triggers graceful shutdown with exit(0), allowing the service manager
 * (systemd, launchd, Windows Service) to automatically restart the server.
 *
 * @param client HTTP client connection
 * @return true if handler processed the request, false otherwise
 */
bool c4_handle_restart_server(client_t* client) {
  if (strcmp(client->request.path, "/api/restart") != 0) return false;
  if (client->request.method != C4_DATA_METHOD_POST) return false;

  // Security: Check if Web-UI is enabled
  if (check_web_ui_access(client)) return true;

  // Send response before shutdown
  buffer_t data = {0};
  bprintf(&data, "{\"success\": true, \"message\": \"Server is restarting gracefully...\"}");
  c4_http_respond(client, 200, "application/json", data.data);
  buffer_free(&data);

  // Log the restart
  fprintf(stderr, "C4 Server: Configuration restart requested via Web UI\n");
  fprintf(stderr, "C4 Server: Initiating graceful shutdown for restart...\n");

  // Trigger graceful shutdown (server will exit with code 0)
  // The service manager (systemd/launchd/Windows Service) will automatically restart
  extern volatile sig_atomic_t graceful_shutdown_in_progress;
  graceful_shutdown_in_progress = 1;

  // Give libuv a chance to send the response before shutting down
  // The graceful shutdown will wait for open requests to complete
  if (http_server.stats.open_requests <= 1) {
    // Only this request is open, proceed immediately
    fprintf(stderr, "C4 Server: No other open requests, proceeding with restart...\n");
    exit(0);
  }

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
  if (check_web_ui_access(client)) return true;

  c4_http_respond(client, 200, "text/html", bytes((void*) config_html, strlen(config_html)));
  return true;
}
