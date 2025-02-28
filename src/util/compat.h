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