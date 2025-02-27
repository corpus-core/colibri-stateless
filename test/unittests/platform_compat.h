#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifdef _MSC_VER
#include <stdlib.h>

static inline int setenv(const char* name, const char* value, int overwrite) {
  (void) overwrite; // Unused parameter
  return _putenv_s(name, value);
}
#endif

#endif // PLATFORM_COMPAT_H