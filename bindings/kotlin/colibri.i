%module colibri

%{
#include "colibri.h"
#include <string.h>
%}

%include "stdint.i"

// Mache SWIG klar, dass der Typ bytearray_t in Java als byte[] dargestellt werden soll:
%typemap(jstype) bytearray_t "byte[]"

// Für Eingabeparameter: Wenn ein byte[] übergeben wird, konvertiert SWIG diesen in ein bytearray_t
%typemap(javain) bytearray_t (jbyteArray jarr) {
#ifdef SWIGJAVA
    jsize _len = env->GetArrayLength($input);
    $1.data = (uint8_t *) malloc(_len);
    if (!$1.data) {
        SWIG_exception_fail(SWIG_MemoryError, "Unable to allocate memory for bytearray_t.data");
    }
    env->GetByteArrayRegion($input, 0, _len, (jbyte*) $1.data);
    $1.len = (size_t)_len;
#endif
}

// Für Rückgabewerte: Konvertiere ein bytearray_t in ein Java byte[]
%typemap(javaout) bytearray_t {
#ifdef SWIGJAVA
    jsize _len = (jsize)$1.len;
    jbyteArray jarr = env->NewByteArray(_len);
    if (!jarr) {
        SWIG_exception_fail(SWIG_MemoryError, "Unable to allocate Java byte array");
    }
    env->SetByteArrayRegion(jarr, 0, _len, (jbyte*) $1.data);
    /* Optional: Falls erforderlich, free($1.data) */
    $result = jarr;
#endif
}

%include "colibri.h"