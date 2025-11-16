#include "../util/chains.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif

static char* get_default_config(char* chain_name, chain_id_t* chain_id, char* config_file) {
  if (!chain_name) chain_name = "mainnet";
  if (*chain_name >= '0' && *chain_name <= '9')
    *chain_id = atoi(chain_name);
  else if (strcmp(chain_name, "mainnet") == 0)
    *chain_id = C4_CHAIN_MAINNET;
  else if (strcmp(chain_name, "sepolia") == 0)
    *chain_id = C4_CHAIN_SEPOLIA;
  else if (strcmp(chain_name, "gnosis") == 0)
    *chain_id = C4_CHAIN_GNOSIS;
  else if (strcmp(chain_name, "chiado") == 0)
    *chain_id = C4_CHAIN_GNOSIS_CHIADO;
  else if (strcmp(chain_name, "base") == 0)
    *chain_id = C4_CHAIN_BASE;
  else {
    fprintf(stderr, "Invalid chain name: %s\n", chain_name);
    exit(EXIT_FAILURE);
  }

  char* config = NULL;

  switch (*chain_id) {
    case 1: // mainnet
      config = "{\"eth_rpc\":[\"https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584\",\"https://ethereum-mainnet.core.chainstack.com/364e0a05996fe175eb1975ddc6e9147d\",\"https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/\",\"https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S\",\"https://rpc.ankr.com/eth/c14449317accec005863d22c7515f6b69667abb29ba2b5e099abf490bcb875b1\",\"https://eth.llamarpc.com\",\"https://rpc.payload.de\",\"https://ethereum-rpc.publicnode.com\"],"
               "\"beacon_api\":[\"https://ethereum-mainnet.core.chainstack.com/beacon/364e0a05996fe175eb1975ddc6e9147d/\",\"http://unstable.mainnet.beacon-api.nimbus.team/\",\"https://lodestar-mainnet.chainsafe.io/\"],"
               "\"checkpointz\":[\"https://sync-mainnet.beaconcha.in\",\"https://beaconstate.info\",\"https://sync.invis.tools\",\"https://beaconstate.ethstaker.cc\"],"
               "\"prover\":[\"https://mainnet1.colibri-proof.tech\"]}";
      break;

    case 11155111: // sepolia
      config = "{\"eth_rpc\":[\"https://ethereum-sepolia-rpc.publicnode.com\"],"
               "\"beacon_api\":[\"https://ethereum-sepolia-beacon-api.publicnode.com/\"]"
               "\"checkpointz\":[],"
               "\"prover\":[\"https://sepolia.colibri-proof.tech\"]}";
      break;
  }

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