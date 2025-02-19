%module colibri

%{
#include "colibri.h"
#include <string.h>
%}

%include "stdint.i"

// Definiere, dass ein bytes_t in Java als byte[] erscheinen soll:
%typemap(jstype) bytearray_t "byte[]"

// Für Eingabeparameter: Wenn in Java ein byte[] übergeben wird, wird ein bytes_t erzeugt.
// Hier füllen wir die Felder data und len.
%typemap(javain) bytearray_t (jbyteArray jarr) {
    jsize _len = env->GetArrayLength($input);
    $1.data = (uint8_t *) malloc(_len);
    if (!$1.data) {
       SWIG_exception_fail(SWIG_MemoryError, "Unable to allocate memory for bytes_t.data");
    }
    env->GetByteArrayRegion($input, 0, _len, (jbyte*) $1.data);
    $1.len = (size_t)_len;
}

// Optional: Falls du den Speicher nach Funktionsaufruf freigeben möchtest – 
// jedoch geht man hier oft davon aus, dass die C-Funktion den Pointer übernimmt.
%typemap(javacleanup) bytearray_t {
    /* Falls erforderlich: free($1.data); */
}


%typemap(javaout) bytearray_t {
    jsize _len = (jsize)$1.len;
    jbyteArray jarr = env->NewByteArray(_len);
    if (!jarr) {
        SWIG_exception_fail(SWIG_MemoryError, "Unable to allocate Java byte array");
    }
    env->SetByteArrayRegion(jarr, 0, _len, (jbyte*) $1.data);
    /* 
       Falls deine API vorschreibt, dass der Rückgabepuffer vom Aufrufer freigegeben werden muss,
       kannst du hier auch free($1.data) aufrufen. Andernfalls, wenn die C-Funktion den Speicher intern
       verwaltet, lass das weg.
    */
    // free($1.data); // Optional, je nach API-Dokumentation.
    $result = jarr;
}

%include "colibri.h"