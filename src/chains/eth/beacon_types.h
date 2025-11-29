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

#ifndef BEACON_TYPES_H
#define BEACON_TYPES_H

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN    0x00000000 // No specific client requirement
#define BEACON_CLIENT_NIMBUS     0x00000001 // (1 << 0)
#define BEACON_CLIENT_LODESTAR   0x00000002 // (1 << 1)
#define BEACON_CLIENT_PRYSM      0x00000004 // (1 << 2)
#define BEACON_CLIENT_LIGHTHOUSE 0x00000008 // (1 << 3)
#define BEACON_CLIENT_TEKU       0x00000010 // (1 << 4)
#define BEACON_CLIENT_GRANDINE   0x00000020 // (1 << 5)

#include <stdint.h>

// Common beacon client type
typedef uint32_t beacon_client_type_t;

#endif // BEACON_TYPES_H
