/**
 * Compatibility header for platform-specific differences
 * This file provides consistent definitions across different platforms
 */

#ifndef UTIL_COMPAT_H
#define UTIL_COMPAT_H

#include <stdint.h>

/* Include standard inttypes.h if available */
#if !defined(EMBEDDED) || defined(__STDC_FORMAT_MACROS)
#include <inttypes.h>
#else
/* Define format macros for embedded targets that may lack proper inttypes.h support */
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