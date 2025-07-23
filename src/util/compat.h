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

/**
 * Compatibility header for platform-specific differences
 * This file provides consistent definitions across different platforms
 */

#ifndef UTIL_COMPAT_H
#define UTIL_COMPAT_H

#include <stdint.h>

/* For non-embedded targets, just include standard inttypes.h */
#if !defined(EMBEDDED)
#include <inttypes.h>
#else
/* For embedded targets, we include inttypes.h but also provide our own definitions
   as a fallback in case the platform's inttypes.h is incomplete */
#ifdef __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

/* Always define our own macros for embedded targets (they won't override if already defined) */
#ifndef PRIx64
#define PRIx64 "llx"
#endif

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#ifndef PRId64
#define PRId64 "lld"
#endif

#ifndef PRIx32
#define PRIx32 "x"
#endif

#ifndef PRIu32
#define PRIu32 "u"
#endif

#ifndef PRId32
#define PRId32 "d"
#endif
#endif /* EMBEDDED */

#endif /* UTIL_COMPAT_H */