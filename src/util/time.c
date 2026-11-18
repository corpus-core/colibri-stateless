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

#include "common.h"
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

uint64_t current_unix_ms() {
  struct timeval time_val;
#ifdef _WIN32
  FILETIME       ft;
  ULARGE_INTEGER large_int;
  GetSystemTimeAsFileTime(&ft);
  large_int.LowPart  = ft.dwLowDateTime;
  large_int.HighPart = ft.dwHighDateTime;
  // Convert to microseconds from Jan 1, 1601, then to the Unix epoch.
  uint64_t unix_time = (large_int.QuadPart - 116444736000000000LL) / 10;
  time_val.tv_sec    = unix_time / 1000000;
  time_val.tv_usec   = unix_time % 1000000;
#else
  gettimeofday(&time_val, NULL);
#endif
  return (uint64_t) time_val.tv_sec * 1000ull + (uint64_t) time_val.tv_usec / 1000ull;
}
