% module c4

    % {
#include "proofer/proofer.h"
#include "util/request.h"
#include "verifier/verify.h"
#include <stdint.h>
          % }

    // Typemaps für bytes_t Struktur
    % include "cstring.i" % include "stdint.i"

    // Behandlung von bytes_t
    typedef struct {
  uint8_t* data;
  size_t   len;
} bytes_t;

% extend bytes_t{
      // Konvertierung zu Python bytes
      PyObject * to_bytes(){
                     return PyBytes_FromStringAndSize((char*) $self->data, $self->len);
}

// Konstruktor aus Python bytes
% pythoncode % {
  @staticmethod
          def
          from_bytes(data) : if not isinstance(data, bytes) : raise TypeError("Expected bytes object") return _c4.bytes_t_from_buffer(data) %
}
}

// Hilfsfunktion für bytes Konvertierung
% inline % {bytes_t * bytes_t_from_buffer(PyObject * bytes){char * data;
Py_ssize_t length;
if (PyBytes_AsStringAndSize(bytes, &data, &length) == -1)
  return NULL;

bytes_t* result = (bytes_t*) malloc(sizeof(bytes_t));
result->data    = (uint8_t*) malloc(length);
memcpy(result->data, data, length);
result->len = length;
return result;
}
%
}

// Behandlung von char* als String
% include "typemaps.i"

    // DataRequest Klasse
    typedef enum {
      DATA_REQUEST_GET,
      DATA_REQUEST_POST
    } data_request_method_t;

typedef enum {
  DATA_REQUEST_BINARY,
  DATA_REQUEST_JSON
} data_request_type_t;

typedef enum {
  DATA_REQUEST_HEX,
  DATA_REQUEST_BASE64
} data_request_encoding_t;

typedef struct data_request {
  data_request_type_t     type;
  data_request_encoding_t encoding;
  char*                   url;
  data_request_method_t   method;
  bytes_t                 payload;
  bytes_t                 response;
  char*                   error;
  struct data_request*    next;
  bytes32_t               id;
} data_request_t;

// Proofer Klasse
typedef enum {
  C4_PROOFER_SUCCESS,
  C4_PROOFER_ERROR
} c4_proofer_status_t;

typedef struct {
  char*           method;
  json_t          params;
  char*           error;
  bytes_t         proof;
  data_request_t* data_requests;
} proofer_ctx_t;

// Proofer Funktionen
% newobject         c4_proofer_create;
proofer_ctx_t*      c4_proofer_create(char* method, char* params);
void                c4_proofer_free(proofer_ctx_t* ctx);
c4_proofer_status_t c4_proofer_execute(proofer_ctx_t* ctx);
data_request_t*     c4_proofer_get_pending_data_request(proofer_ctx_t* ctx);

// Verify Funktion
% newobject   c4_create_and_verify_from_bytes;
verify_ctx_t* c4_create_and_verify_from_bytes(void* start_data, int len_data, char* method, char* args);

// Python-spezifische Erweiterungen
% pythoncode % {
  class Proofer : def __init__(self, method, params) : self._ctx = c4_proofer_create(method, params) if not self._ctx : raise RuntimeError("Failed to create proofer context")

                                                                                                                            def __del__(self) : if hasattr (self, '_ctx') and
                                                                   self._ctx : c4_proofer_free(self._ctx)

                                                                                   def execute(self) : status = c4_proofer_execute(self._ctx) if status != C4_PROOFER_SUCCESS : raise RuntimeError("Proofer execution failed")

                                                                                                                                                                                    def get_pending_request(self) : req = c4_proofer_get_pending_data_request(self._ctx) if not req : return None return DataRequest(req)

                                                                                                                                                                                                                                                                                          class DataRequest : def __init__(self, req) : self._req = req

                                                                                                                                                                                                                                                                                                                                                    @property
                                                                                                                                                                                                                                                                                                                                                        def url(self) : return self._req.url

                                                                                                                                                                                                                                                                                                                                                                        @property
                                                                                                                                                                                                                                                                                                                                                                            def method(self) : return self._req.method

                                                                                                                                                                                                                                                                                                                                                                                               @property
                                                                                                                                                                                                                                                                                                                                                                                                   def payload(self) : return bytes(self._req.payload.to_bytes())

                                                                                                                                                                                                                                                                                                                                                                                                                           @property
                                                                                                                                                                                                                                                                                                                                                                                                                               def response(self) : return bytes(self._req.response.to_bytes())

                                                                                                                                                                                                                                                                                                                                                                                                                                                        @property
                                                                                                                                                                                                                                                                                                                                                                                                                                                            def error(self) : return self._req.error %
}