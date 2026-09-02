#include "../util/chains.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif
#include "default_chains.generated.h"

static char* get_default_config(char* chain_name, chain_id_t* chain_id, char* config_file) {
  if (!chain_name) chain_name = "mainnet";
  if (chain_name[0] == '0' && (chain_name[1] == 'x' || chain_name[1] == 'X'))
    *chain_id = (chain_id_t) strtoull(chain_name, NULL, 16);
  else if (*chain_name >= '0' && *chain_name <= '9')
    *chain_id = (chain_id_t) atoll(chain_name);
  else if (!c4_default_chain_id_from_name(chain_name, chain_id)) {
    if (strcmp(chain_name, "base") == 0)
      *chain_id = C4_CHAIN_BASE;
    else {
      fprintf(stderr, "Invalid chain name: %s\n", chain_name);
      exit(EXIT_FAILURE);
    }
  }

  char* config = (char*) c4_default_chain_config_json(*chain_id);

#ifdef USE_CURL
  if (config) curl_set_config(json_parse(config));

  if (config_file) {
    config = (char*) bytes_read(config_file).data;
    curl_set_config(json_parse(config));
  }
#endif
  return config;
}

static void set_config(char* target, char* urls) {
  char* config = NULL;

  // Build JSON object { "<target>": [ ...urls... ] }
  if (!urls)
    return;
  else if (strchr(urls, ',')) {
    buffer_t    buf   = {0};
    const char* start = urls;
    const char* p     = urls;
    bool        first = true;

    bprintf(&buf, "{\"%s\":[", target);
    for (;; p++) {
      if (*p == ',' || *p == '\0') {
        const char* tstart = start;
        const char* tend   = p;
        while (tstart < tend && (*tstart == ' ' || *tstart == '\t' || *tstart == '\n' || *tstart == '\r')) tstart++;
        while (tend > tstart && (tend[-1] == ' ' || tend[-1] == '\t' || tend[-1] == '\n' || tend[-1] == '\r')) tend--;
        if (tend > tstart) {
          if (!first) bprintf(&buf, ",");
          size_t len = (size_t) (tend - tstart);
          bprintf(&buf, "\"%r\"", bytes(tstart, len));
          first = false;
        }
        if (*p == '\0') break;
        start = p + 1;
      }
    }
    config = bprintf(&buf, "]}");
  }
  else {
    // Single URL
    config = bprintf(NULL, "{\"%s\":[\"%S\"]}", target, urls);
  }
#ifdef USE_CURL
  curl_set_config(json_parse(config));
#endif
  safe_free(config);
}