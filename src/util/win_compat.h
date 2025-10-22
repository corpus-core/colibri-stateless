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

#ifndef WIN_COMPAT_H
#define WIN_COMPAT_H

#ifdef _WIN32

#include <string.h>
#include <stdlib.h>

// Windows equivalents for POSIX functions

// setenv -> _putenv_s
static inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) {
        return 0;
    }
    return _putenv_s(name, value);
}

// strndup - not available on Windows
static inline char* strndup(const char* s, size_t n) {
    size_t len = strnlen(s, n);
    char* result = (char*)malloc(len + 1);
    if (result) {
        memcpy(result, s, len);
        result[len] = '\0';
    }
    return result;
}

// strncasecmp -> _strnicmp
#define strncasecmp _strnicmp

// strcasecmp -> _stricmp
#define strcasecmp _stricmp

// memmem - GNU extension, not available on Windows
static inline void* memmem(const void* haystack, size_t haystacklen,
                           const void* needle, size_t needlelen) {
    if (needlelen == 0) {
        return (void*)haystack;
    }
    if (haystacklen < needlelen) {
        return NULL;
    }
    
    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;
    size_t i;
    
    for (i = 0; i <= haystacklen - needlelen; i++) {
        if (memcmp(h + i, n, needlelen) == 0) {
            return (void*)(h + i);
        }
    }
    return NULL;
}

#endif // _WIN32

#endif // WIN_COMPAT_H

