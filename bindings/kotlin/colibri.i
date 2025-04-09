%module c4

%{
#include "colibri.h"
%}

// Include standard typemaps
%include "stdint.i"
%include "various.i"

// IMPORTANT: Define the bytes_t structure for SWIG
// This needs to be before the typemaps so the wrapper code can refer to it
%{
#ifndef BYTES_T_DEFINED
typedef struct bytes_t {
  uint8_t* data;
  uint32_t   len;
} bytes_t;
#define BYTES_T_DEFINED
#endif
%}

// Add typemap for char* return values that need to be freed
%typemap(out) char* c4_verify_execute_json_status, char* c4_proofer_execute_json_status {
    if ($1) {
        $result = (*jenv)->NewStringUTF(jenv, (const char *)$1);
        free($1);  // Free the dynamically allocated string
    }
}

// We don't want SWIG to create a Java class for bytes_t
// Instead, we need to map it to byte[] in Java but keep the struct definition for C
%ignore bytes_t;

// For c4_req_set_response - converting Java byte[] to bytes_t
%typemap(jni) bytes_t "jbyteArray"
%typemap(jtype) bytes_t "byte[]"
%typemap(jstype) bytes_t "byte[]"
%typemap(javain) bytes_t "$javainput"

%typemap(in) bytes_t {
    bytes_t array;
    array.data = (uint8_t*)JCALL2(GetByteArrayElements, jenv, $input, 0);
    array.len = JCALL1(GetArrayLength, jenv, $input);
    $1 = array;
}

%typemap(freearg) bytes_t {
    JCALL3(ReleaseByteArrayElements, jenv, $input, (jbyte*)$1.data, JNI_ABORT);
}

// For return values - converting bytes_t to Java byte[]
%typemap(jni) bytes_t "jbyteArray"
%typemap(jtype) bytes_t "byte[]"
%typemap(jstype) bytes_t "byte[]"
%typemap(javaout) bytes_t { return $jnicall; }

%typemap(out) bytes_t {
    $result = JCALL1(NewByteArray, jenv, $1.len);
    JCALL4(SetByteArrayRegion, jenv, $result, 0, $1.len, (jbyte*)$1.data);
}

// Handle the void* type properly
%typemap(jni) void* "jlong"
%typemap(jtype) void* "long"
%typemap(jstype) void* "long"
%typemap(javain) void* "$javainput"
%typemap(javaout) void* { return $jnicall; }

// Handle proofer_t* as an opaque pointer
%typemap(jni) proofer_t* "jlong"
%typemap(jtype) proofer_t* "long"
%typemap(jstype) proofer_t* "long"
%typemap(javain) proofer_t* "$javainput"
%typemap(javaout) proofer_t* { return $jnicall; }

// --- Add native bridge initialization function ---
// This function is implemented in jni_bridge.c, not the core library.
// We declare it here so SWIG generates the JNI wrapper for it in c4JNI.
%native(nativeInitializeBridge) void nativeInitializeBridge();

// Now include the full header
%include "colibri.h"