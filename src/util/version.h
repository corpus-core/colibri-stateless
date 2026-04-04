/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef C4_VERSION_H
#define C4_VERSION_H

#define VERSION_MAJOR 0
#define VERSION_MINOR 2
#define VERSION_PATCH 0
#define CHAIN_TYPE    1 // ETH=1

#include <stdint.h>
#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
#define C4_VERSION_PURE __attribute__((pure))
#else
#define C4_VERSION_PURE
#endif

// the Version of the Protocol used when creating proof. This should only be changed, if the proof format changes.
extern const uint8_t c4_protocol_version_bytes[4];

// the client-version, which should be set during the build-process.
extern const char* c4_client_version;

/**
 * Parse major, minor and patch from c4_client_version and combine into a single
 * uint32_t: (major << 16) | (minor << 8) | patch. Each component is clamped to
 * 0..255. Returns 0 if the version string cannot be parsed.
 * Uses the C4_VERSION macro directly so the compiler constant-folds the result.
 * Implemented without sscanf for static-analysis safety.
 *
 * @return Version number or 0 on parse failure
 */
C4_VERSION_PURE uint32_t c4_current_version_number(void);

static inline uint32_t c4_version_number(const uint8_t major, const uint8_t minor, const uint8_t patch) {
  return ((uint32_t)major << 16) | ((uint32_t)minor << 8) | (uint32_t)patch;
}

/**
 * Print version information and build flags to the specified file stream.
 *
 * @param out Output stream (e.g., stdout or stderr)
 * @param program_name Name of the program (e.g., "colibri-server")
 */
void c4_print_version(FILE* out, const char* program_name);

#endif
