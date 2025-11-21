#ifndef SERVERIO_CONFIGURE_H
#define SERVERIO_CONFIGURE_H

#include "crypto.h"

typedef enum {
  CONFIG_PARAM_INT,
  CONFIG_PARAM_STRING,
  CONFIG_PARAM_KEY
} config_param_type_t;

typedef struct {
  char*               name;        // env variable name
  char*               arg_name;    // command line arg name
  char*               description; // human-readable description
  config_param_type_t type;        // parameter type
  void*               value_ptr;   // pointer to actual value
  int                 min;         // min value (for int)
  int                 max;         // max value (for int)
} config_param_t;

const config_param_t* c4_get_config_params(int* count);
void                  c4_configure(int argc, char* argv[]);
void                  c4_write_usage();
void                  c4_init_config(int argc, char* argv[]);
void                  c4_write_config();

int conf_string(char** target, char* env_name, char* arg_nane, char shortcut, char* descr);
int conf_key(bytes32_t target, char* env_name, char* arg_nane, char shortcut, char* descr);
int conf_int(int* target, char* env_name, char* arg_nane, char shortcut, char* descr, int min, int max);
#define conf_bool(target, env_name, arg_nane, shortcut, descr) conf_int((int*) target, env_name, arg_nane, shortcut, descr, 0, 1)

#endif