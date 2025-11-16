/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * File-based mock system for server tests
 * Replaces URLs with file:// URLs pointing to test data
 *
 * SECURITY: This file should ONLY be included in TEST builds
 */

#ifndef FILE_MOCK_HELPER_H
#define FILE_MOCK_HELPER_H

#ifndef TEST
#error "file_mock_helper.h should only be included in TEST builds"
#endif

#include "util/bytes.h"
#include "util/common.h"
#include "util/crypto.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// Global test directory - set this to enable file-based mocking
static char* c4_test_data_dir       = NULL;
static bool  c4_test_recording_mode = false;

static void c4_file_mock_init(const char* test_data_dir, bool recording_mode) {
  c4_test_data_dir       = test_data_dir ? strdup(test_data_dir) : NULL;
  c4_test_recording_mode = recording_mode;
}

static void c4_file_mock_cleanup(void) {
  if (c4_test_data_dir) {
    free(c4_test_data_dir);
    c4_test_data_dir = NULL;
  }
  c4_test_recording_mode = false;
}

static void c4_file_mock_seed_random(uint32_t seed) {
  srand(seed);
}

// Create directory recursively
static void ensure_directory(const char* path) {
  char   tmp[512];
  char*  p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);

  if (tmp[len - 1] == '/') {
    tmp[len - 1] = 0;
  }

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

// Forward declaration - implemented in http_client.c (server component)
// This avoids code duplication between test helpers and production recording
extern char* c4_file_mock_get_filename(const char* host, const char* url,
                                       const char* payload, const char* test_name);

static char* c4_file_mock_replace_url(const char* original_url, const char* payload,
                                      const char* test_name) {
  if (!test_name || !original_url) return strdup(original_url);

  // Extract host from URL (e.g., "http://host:port/path" -> "host")
  char*       host       = NULL;
  const char* host_start = strstr(original_url, "://");
  if (host_start) {
    host_start += 3; // Skip "://"
    const char* host_end = strchr(host_start, ':');
    if (!host_end) host_end = strchr(host_start, '/');
    if (host_end) {
      size_t len = host_end - host_start;
      host       = strndup(host_start, len);
    }
    else {
      host = strdup(host_start);
    }
  }

  // Use central filename generation (implemented in http_client.c)
  char* filename = c4_file_mock_get_filename(host, original_url, payload, test_name);
  if (!filename) {
    free(host);
    return strdup(original_url);
  }

  // Build file:// URL
  char* file_url = bprintf(NULL, "file://%s", filename);

  free(host);
  free(filename);

  return file_url;
}

#endif // FILE_MOCK_HELPER_H
