/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#ifndef C4_VISIBILITY_H // Include guard for the header file
#define C4_VISIBILITY_H

// Check for GCC or Clang (which often defines __GNUC__ too)
#if defined(__GNUC__) && (__GNUC__ >= 4)
// Visibility("hidden") is available starting GCC 4
#define INTERNAL   __attribute__((visibility("hidden")))
#define API_PUBLIC __attribute__((visibility("default"))) // Optional: Explicitly mark public API
#elif defined(__GNUC__)                                   // Older GCC/Clang without visibility support
#define INTERNAL
#define API_PUBLIC
#elif defined(_MSC_VER)
// MSVC doesn't have a direct equivalent for 'hidden' for static libs.
// Rely on 'static' keyword for file-local symbols.
// Rely on prefixes for internal symbols shared between .c files.
#define INTERNAL
// For DLLs, you'd use __declspec(dllexport/dllimport) here for API_PUBLIC
#define API_PUBLIC
#else // Other compilers
#define INTERNAL
#define API_PUBLIC
#endif

// Marks a symbol as potentially unused to avoid compiler warnings.
// On MSVC this expands to nothing; on GCC/Clang it uses __attribute__((unused)).
#if defined(__GNUC__) || defined(__clang__)
#define C4_UNUSED __attribute__((unused))
#else
#define C4_UNUSED
#endif

/**
 * Linkage of a function that production code calls only inside its own file
 * and that unit tests call from another file.
 *
 * CMake defines `TEST` for test builds (`-DTEST`). The macro is then empty, so
 * the function has external linkage and the test can link against it. A build
 * without `TEST` makes the function `static`: the compiler may inline it, and
 * the symbol is not exported. Put the macro on the declaration and on the
 * definition. A caller in a second file of a release build needs an ordinary
 * exported function; this macro does not cover that case.
 *
 * ```c
 * C4_STATIC_UNLESS_TEST c4_status_t parse_internal(bytes_t raw);
 * ```
 */
#ifdef TEST
#define C4_STATIC_UNLESS_TEST
#else
#define C4_STATIC_UNLESS_TEST static
#endif

#include <stdint.h>

static inline uint32_t min32(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static inline uint64_t min64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static inline uint64_t max64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

#endif // C4_VISIBILITY_H