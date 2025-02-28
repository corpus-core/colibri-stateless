/**
 * Compatibility header for embedded environments
 * This is a wrapper to include the main compatibility header
 *
 * Note: This header exists primarily to maintain backward compatibility
 * with any test code that might be including it. New code should directly
 * include src/util/compat.h instead.
 */

#ifndef EMBEDDED_COMPAT_H
#define EMBEDDED_COMPAT_H

/* Include the main compatibility header */
#include "../../src/util/compat.h"

#endif /* EMBEDDED_COMPAT_H */