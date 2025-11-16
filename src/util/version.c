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

#include "version.h"
#include <stdio.h>

const uint8_t c4_protocol_version_bytes[4] = {CHAIN_TYPE, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH};

// Version is set by CMake via -DC4_VERSION="..."
#ifndef C4_VERSION
#define C4_VERSION "0.1.0-dev"
#endif

const char* c4_client_version = C4_VERSION;

void c4_print_version(FILE* out, const char* program_name) {
  fprintf(out, "%s version %s\n", program_name, c4_client_version);
  fprintf(out, "\nBuild Configuration:\n");

  // Core features
  fprintf(out, "  Protocol Version: %d.%d.%d\n",
          VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

#ifdef USE_CURL
  fprintf(out, "  CURL Support:     enabled\n");
#else
  fprintf(out, "  CURL Support:     disabled\n");
#endif

#ifdef PROVER_CACHE
  fprintf(out, "  Prover Cache:     enabled\n");
#else
  fprintf(out, "  Prover Cache:     disabled\n");
#endif

#ifdef TEST
  fprintf(out, "  Test Mode:        enabled\n");
#else
  fprintf(out, "  Test Mode:        disabled\n");
#endif

#ifdef MESSAGES
  fprintf(out, "  Error Messages:   enabled\n");
#else
  fprintf(out, "  Error Messages:   disabled\n");
#endif

#ifdef BLS_DESERIALIZE
  fprintf(out, "  BLS Deserialize:  enabled\n");
#else
  fprintf(out, "  BLS Deserialize:  disabled\n");
#endif

#ifdef HTTP_SERVER
  fprintf(out, "  HTTP Server:      enabled\n");
#else
  fprintf(out, "  HTTP Server:      disabled\n");
#endif

#ifdef HTTP_SERVER_GEO
  fprintf(out, "  GeoIP Support:    enabled\n");
#else
  fprintf(out, "  GeoIP Support:    disabled\n");
#endif

#ifdef STATIC_MEMORY
  fprintf(out, "  Static Memory:    enabled\n");
#else
  fprintf(out, "  Static Memory:    disabled\n");
#endif

#ifdef WASM
  fprintf(out, "  WebAssembly:      enabled\n");
#endif

#ifdef EMBEDDED
  fprintf(out, "  Embedded Target:  enabled\n");
#endif

  // Chain support
  fprintf(out, "\nChain Support:\n");
#ifdef ETH_VERIFICATION
  fprintf(out, "  Ethereum:         enabled\n");
#else
  fprintf(out, "  Ethereum:         disabled\n");
#endif

#ifdef OP_VERIFICATION
  fprintf(out, "  OP Stack:         enabled\n");
#else
  fprintf(out, "  OP Stack:         disabled\n");
#endif

  fprintf(out, "\nCopyright (c) 2025 corpus.core\n");
  fprintf(out, "License: PolyForm Noncommercial 1.0.0\n");
}
